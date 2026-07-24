#!/usr/bin/env python3
# Capture-oracle diff. Usage: verify.py <ref.png> <new.png>
# Behaviour-preserving refactors should diff within the particle noise floor
# (~5k px at seed=1/f200); structural regressions change far more.
import sys
from PIL import Image, ImageChops
ref, new = Image.open(sys.argv[1]).convert('RGB'), Image.open(sys.argv[2]).convert('RGB')
d = ImageChops.difference(ref, new)
changed = sum(1 for p in d.get_flattened_data() if p != (0, 0, 0))
total = ref.size[0] * ref.size[1]
print(f'changed {changed}/{total} ({100*changed/total:.2f}%) bbox {d.getbbox()}')
print('WITHIN NOISE FLOOR' if changed < 8000 else 'REGRESSION SUSPECTED')
