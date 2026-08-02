/* Vulkan Implementation Survival Kit — theme switch.
   Offline by design: no dependencies, no network. Loaded synchronously from
   <head> so a stored theme lands on <html> before the first paint.

   The mode is held in a variable, not re-read from storage: this kit is opened
   over file://, where localStorage is allowed to throw. Persistence is a bonus;
   switching has to work without it. */
(function () {
  "use strict";

  var KEY = "vk-kit-theme";
  var MODES = ["auto", "light", "dark"];
  var mode = "auto";

  try {
    var saved = window.localStorage.getItem(KEY);
    if (MODES.indexOf(saved) > 0) mode = saved;
  } catch (e) {
    /* no storage — this session starts on auto */
  }

  function apply() {
    if (mode === "auto") {
      delete document.documentElement.dataset.theme;
    } else {
      document.documentElement.dataset.theme = mode;
    }
  }

  function persist() {
    try {
      if (mode === "auto") {
        window.localStorage.removeItem(KEY);
      } else {
        window.localStorage.setItem(KEY, mode);
      }
    } catch (e) {
      /* not fatal — the page still switches for this session */
    }
  }

  apply();

  function wire() {
    var button = document.querySelector("[data-theme-toggle]");
    if (!button) return;

    var label = button.querySelector(".mode");

    function show() {
      var next = MODES[(MODES.indexOf(mode) + 1) % MODES.length];
      label.textContent = mode;
      button.setAttribute(
        "title",
        (mode === "auto" ? "Theme follows your system setting." : "Theme is " + mode + ".") +
          " Switch to " + next + "."
      );
    }

    show();

    button.addEventListener("click", function () {
      mode = MODES[(MODES.indexOf(mode) + 1) % MODES.length];
      apply();
      persist();
      show();
    });
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", wire);
  } else {
    wire();
  }
})();
