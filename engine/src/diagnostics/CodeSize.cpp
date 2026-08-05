#include <eng/CodeSize.h>

#include <eng/Log.h>

#include <algorithm>
#include <cstring>
#include <map>

#if defined(__linux__)
#    include <cxxabi.h>
#    include <elf.h>
#    include <fcntl.h>
#    include <sys/mman.h>
#    include <sys/stat.h>
#    include <unistd.h>
#    define ENG_CODESIZE_ELF 1
#else
#    define ENG_CODESIZE_ELF 0
#endif

namespace eng::codesize {
namespace {

bool gAvailable = false;

#if ENG_CODESIZE_ELF

std::string demangle(const char* mangled)
{
    int status = 0;
    char* out = abi::__cxa_demangle(mangled, nullptr, nullptr, &status);
    std::string result = (status == 0 && out) ? out : mangled;
    std::free(out);
    return result;
}

// A mapped file that unmaps itself. The symbol table of a Debug build of this
// engine is tens of megabytes; reading it into a vector would double that for
// the length of one function call.
struct Mapping {
    const unsigned char* data = nullptr;
    std::size_t size = 0;

    explicit Mapping(const char* path)
    {
        const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0)
            return;
        struct stat info {};
        if (::fstat(fd, &info) == 0 && info.st_size > 0) {
            void* mapped = ::mmap(nullptr, static_cast<std::size_t>(info.st_size),
                                  PROT_READ, MAP_PRIVATE, fd, 0);
            if (mapped != MAP_FAILED) {
                data = static_cast<const unsigned char*>(mapped);
                size = static_cast<std::size_t>(info.st_size);
            }
        }
        ::close(fd);
    }
    ~Mapping()
    {
        if (data)
            ::munmap(const_cast<unsigned char*>(data), size);
    }
    Mapping(const Mapping&) = delete;
    Mapping& operator=(const Mapping&) = delete;

    // Every read below is bounds-checked against the mapped length rather than
    // trusted from the header: this parses a file, and a parser that trusts its
    // input is a crash waiting for a truncated binary.
    template <typename T>
    const T* at(std::size_t offset, std::size_t count = 1) const
    {
        if (!data || offset > size || (size - offset) / sizeof(T) < count)
            return nullptr;
        return reinterpret_cast<const T*>(data + offset);
    }
};

// Streams every (name, size) pair to `visit`, retaining nothing.
//
// Deliberately not "read once into a cached vector": this binary has 108k
// function symbols, and holding their demangled names costs about 5 MB for the
// life of the process -- which the heap profiler duly reported as the single
// largest allocation site in the engine. A tool that is the biggest thing in
// its own report is measuring itself. The aggregations below each make one
// pass and keep only their result.
template <typename Visit>
void forEachSymbol(Visit&& visit)
{
    Mapping elf("/proc/self/exe");
    const auto* header = elf.at<Elf64_Ehdr>(0);
    if (!header || std::memcmp(header->e_ident, ELFMAG, SELFMAG) != 0 ||
        header->e_ident[EI_CLASS] != ELFCLASS64)
        return;

    const auto* sections =
        elf.at<Elf64_Shdr>(header->e_shoff, header->e_shnum);
    if (!sections)
        return;

    for (unsigned i = 0; i < header->e_shnum; ++i) {
        // .symtab, not .dynsym: the dynamic table holds only exported symbols,
        // which in a game binary is almost nothing. The static one has every
        // function, which is the whole point.
        if (sections[i].sh_type != SHT_SYMTAB || sections[i].sh_entsize == 0)
            continue;
        if (sections[i].sh_link >= header->e_shnum)
            continue;
        const Elf64_Shdr& strings = sections[sections[i].sh_link];
        const std::size_t count = sections[i].sh_size / sections[i].sh_entsize;
        const auto* symbols = elf.at<Elf64_Sym>(sections[i].sh_offset, count);
        const char* names = elf.at<char>(strings.sh_offset, strings.sh_size);
        if (!symbols || !names)
            continue;

        for (std::size_t s = 0; s < count; ++s) {
            const Elf64_Sym& sym = symbols[s];
            if (ELF64_ST_TYPE(sym.st_info) != STT_FUNC || sym.st_size == 0)
                continue;
            if (sym.st_name == 0 || sym.st_name >= strings.sh_size)
                continue;
            visit(names + sym.st_name, sym.st_size);
        }
        gAvailable = true;
        return;
    }
}

#else

template <typename Visit>
void forEachSymbol(Visit&&)
{
}

std::string demangle(const char* name)
{
    return name;
}

#endif

// The owning namespace/class of a demangled name: everything up to the last
// `::` that is not inside template arguments or a parameter list. `eng::rhi::
// Vulkan::draw(Batch const&)` becomes `eng::rhi::Vulkan`.
std::string ownerOf(const std::string& raw)
{
    // A demangled template function leads with its return type -- `void
    // std::vector<eng::Batch>::push_back(...)` -- and the owner is in the part
    // after it. Splitting at the first space would land inside the template
    // arguments (`std::pair<int, float>`), so the split is the first space at
    // depth zero, and it is why entries read `std::vector` rather than `void
    // std` and `std::vector<eng`.
    std::size_t begin = 0;
    {
        int d = 0;
        for (std::size_t i = 0; i < raw.size(); ++i) {
            const char c = raw[i];
            if (c == '<' || c == '(' || c == '[')
                ++d;
            else if (c == '>' || c == ')' || c == ']')
                --d;
            else if (c == ' ' && d == 0) {
                begin = i + 1;
                break;
            }
            if (d > 0 && c == '(')
                break;
        }
    }
    const std::string name = raw.substr(begin);

    int depth = 0;
    std::size_t lastScope = std::string::npos;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (c == '<' || c == '(' || c == '[')
            ++depth;
        else if (c == '>' || c == ')' || c == ']')
            --depth;
        else if (depth == 0 && c == ':' && i + 1 < name.size() &&
                 name[i + 1] == ':')
            lastScope = i;
        // Past the parameter list nothing can name a scope, and a `::` in a
        // return type or an argument is not this function's owner.
        if (depth == 0 && c == '(')
            break;
    }
    if (lastScope == std::string::npos)
        return "(free functions)";

    // Two components is the useful granularity: `eng::Renderer` rather than
    // `eng` (which is the whole engine) or `eng::Renderer::Impl` (which is a
    // hundred one-line entries).
    std::string owner = name.substr(0, lastScope);
    int seen = 0, d = 0;
    for (std::size_t i = 0; i + 1 < owner.size(); ++i) {
        const char c = owner[i];
        if (c == '<' || c == '(' || c == '[')
            ++d;
        else if (c == '>' || c == ')' || c == ']')
            --d;
        else if (d == 0 && c == ':' && owner[i + 1] == ':' && ++seen == 2) {
            owner.resize(i);
            break;
        }
    }
    // Whatever survived, cut at the first template argument: `std::vector<T>`
    // and `std::vector<U>` are one module for this purpose, and keeping them
    // apart produces a thousand tiles of one instantiation each.
    if (const std::size_t angle = owner.find('<'); angle != std::string::npos)
        owner.resize(angle);
    return owner.empty() ? "(free functions)" : owner;
}

} // namespace

std::vector<Symbol> functions()
{
    std::vector<Symbol> out;
    forEachSymbol([&](const char* name, std::uint64_t bytes) {
        out.push_back({demangle(name), bytes});
    });
    std::sort(out.begin(), out.end(), [](const Symbol& a, const Symbol& b) {
        return a.bytes > b.bytes;
    });
    return out;
}

bool available()
{
    // One pass that keeps nothing, rather than functions() and its 108k
    // strings, purely to answer a yes/no question.
    static const bool once = [] {
        forEachSymbol([](const char*, std::uint64_t) {});
        return gAvailable;
    }();
    return once;
}

const std::vector<Symbol>& byOwner()
{
    // Cached, unlike functions(), and the difference is the point: demangling
    // 108k symbols takes ~290 ms, which is not a thing to do on a frame, and
    // the symbol table cannot change while the process runs -- so doing it more
    // than once buys nothing. What is kept is only the AGGREGATE: a couple of
    // thousand module names, tens of kilobytes, against the ~5 MB that keeping
    // every function name would cost.
    //
    // Calling this the first time is the expensive one. Warm it somewhere a
    // 290 ms pause does not matter (see warm(), which the app calls during
    // load); every later call is a reference to this vector.
    static const std::vector<Symbol> cached = [] {
        std::map<std::string, std::uint64_t> totals;
        forEachSymbol([&](const char* name, std::uint64_t bytes) {
            // The demangled string is a temporary: only the owner it maps to is
            // kept, and there are far fewer of those than functions.
            totals[ownerOf(demangle(name))] += bytes;
        });

        std::vector<Symbol> out;
        out.reserve(totals.size());
        for (const auto& [name, bytes] : totals)
            out.push_back({name, bytes});
        std::sort(out.begin(), out.end(), [](const Symbol& a, const Symbol& b) {
            return a.bytes > b.bytes;
        });
        return out;
    }();
    return cached;
}

void warm()
{
    byOwner();
}

std::uint64_t codeBytes()
{
    std::uint64_t total = 0;
    for (const Symbol& s : byOwner())
        total += s.bytes;
    return total;
}

std::uint64_t totalBytes()
{
    std::uint64_t total = 0;
    forEachSymbol([&](const char*, std::uint64_t bytes) { total += bytes; });
    return total;
}

std::size_t functionCount()
{
    std::size_t count = 0;
    forEachSymbol([&](const char*, std::uint64_t) { ++count; });
    return count;
}

} // namespace eng::codesize
