# wiifs
**A Wii NAND file system driver**

## Features

wiifs supports most of the file system and file interface exposed by IOS, including
metadata and file read/write, HMAC verification and generation, and ECC data generation.

Some exceptions:

* boot2-related functions (`/dev/boot2`)
* Undocumented commands such as `/dev/fs` ioctlv 14 and `SetFileVersionControl`

wiifs strives to behave as close as possible to IOS's file system driver so
that NAND images which are used with this library can also still be used with IOS.

## Usage

```C++
#include <wiifs/fs.h>

// Map the NAND image to memory, using mmap or any equivalent API.
auto* nand =
  static_cast<u8*>(mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)));

// Fill key information.
wiifs::FileSystemKeys keys;
keys.hmac = nand_hmac_key;
keys.aes = nand_aes_key;

// Create a wiifs::FileSystem.
std::unique_ptr<wiifs::FileSystem> fs = wiifs::FileSystem::Create(nand, keys);
```

Before using any of the file system or file functions, a file descriptor must be
obtained using `FileSystem::OpenFs` or `FileSystem::OpenFile`.

```C++
constexpr wiifs::Uid uid = 0;
constexpr wiifs::Gid gid = 0;
const auto fs_fd = fs->OpenFs(uid, gid);
if (!fs_fd)
  // ...

const auto result = fs->ReadDirectory(*fs_fd, "/");
if (!result)
  // ...
```

For more information about the API, please refer to [`wiifs/fs.h`](include/wiifs/fs.h).

## License

wiifs is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
