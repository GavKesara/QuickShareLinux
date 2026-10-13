# murmur3 build notes

- Source: https://github.com/aappleby/smhasher, `src/MurmurHash3.{h,cpp}` (renamed `.cpp` to `.cc`). The smhasher archive in the Bazel cache has no LICENSE file.
- License: MurmurHash3 is public domain (author disclaims copyright, per the file header). The smhasher README states SMHasher itself is MIT; only MurmurHash3 is vendored here.
- Compile: `clang++ -std=c++20 -c -Wno-implicit-fallthrough -I<QuickshareLinux root> third_party/murmur3/MurmurHash3.cc`. The upstream Bazel target uses `copts = ["-Wno-implicit-fallthrough"]`; keep this flag. The switch-case fallthroughs in the tail handling are intentional.
- Include path: `MurmurHash3.cc` includes `"third_party/murmur3/MurmurHash3.h"`, so the repo root must be on the include path.
- Consumer mismatch: `/home/suhasdissa/Projects/nearby/connections/implementation/mediums/ble/bloom_filter.cc:20` includes `"src/MurmurHash3.h"`. That will not resolve against this directory without changing the include or adding an `-I` for the smhasher `src/` directory.
