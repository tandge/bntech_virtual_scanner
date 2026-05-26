# Image Source Folder Design

Design notes for loading and round-robin iterating images from `%APPDATA%\bntech\images` as the virtual scanner's image source in BN Tech Virtual Scanner.

<details open>
<summary>中文说明</summary>

## 1. 需求

虚拟扫描仪不能没有"被扫描的稿件"。本项目以一个本地图片目录作为虚拟纸张来源，每次应用触发扫描时，DS 从目录里挑出"下一张"图片并按当前 settings（DPI / 像素类型 / 页面尺寸）输出。

主要功能需求：

- 从固定目录 `%APPDATA%\bntech\images\` 读取候选图片。
- 支持常见输入格式：PNG、JPG、JPEG、BMP、TIF、TIFF。
- 文件按字母序（不区分大小写、按 locale 稳定排序）排列；每次扫描自动前进到下一张。
- 列表末尾后回绕到第一张（round-robin），以支持长时间循环测试。
- 扫描索引必须持久化，DLL 被 unload / 重新 load 后仍能从上次的位置继续。
- 当目录不存在 / 为空 / 全是不支持格式时，必须有可用的兜底图，不能让应用扫描失败。兜底图使用 DS 安装目录下的 `TWAIN_logo.png`。
- 重置遍历进度的方式必须简单：只需删除一个文件（`info.json`），无需任何 UI 操作。
- 允许用户在扫描会话期间增删图片，下一次扫描时新目录状态生效。
- 必须线程安全：UI 进程、TWAIN 主线程、可能的 strip 复制线程都可能间接访问。
- ADF 多页场景下（未来扩展），需要支持"一次扫描会话内连续取多张"，仍然按字母序。

非功能性需求：

- 索引文件格式必须可读、易调试（人类肉眼读得懂、能手动修改 / 删除）。
- 不引入外部 JSON 库依赖；如果可能就手写极简 JSON 读写或退化为纯文本。
- 加载图片本身仍走 FreeImage，不增加额外解码库。
- 遍历不依赖目录修改时间戳，避免不同文件系统时间精度差异。

## 2. 领域知识

### 2.1 TWAIN 扫描会话与"下一张"语义

TWAIN 一次完整扫描会话简化序列：

```
OpenDS → EnableDS → (XferReady) → DAT_IMAGEINFO → DAT_IMAGE{NATIVE|FILE}XFER → DAT_PENDINGXFERS → DisableDS → CloseDS
```

`DAT_PENDINGXFERS` 的 `Count` 字段表示扫描仪声明"还有多少页可取"。对平板扫描仪通常是 0（取完这张就结束）；对 ADF 则可能是 -1（未知，继续）或具体数字。本项目模拟平板，每次扫描出一张图，`Count = 0`。

"下一张"指的是：每次新的扫描会话（即每次 `EnableDS` 之后第一次 `DAT_IMAGENATIVEXFER` / `DAT_IMAGEFILEXFER`）从目录里取下一张图片。同一会话内不会取多张。

### 2.2 Windows 用户特殊目录与 `%APPDATA%`

`%APPDATA%` 通常解析为 `C:\Users\<user>\AppData\Roaming`。用户级数据、配置、缓存放这里既能符合 Windows 约定，又不需要管理员权限：

- 普通进程读写自由。
- 不同 Windows 账户互不影响。
- 卸载 DS 时不删用户文件，符合 MSI 卸载最佳实践。

Win32 拿这个目录的标准 API：

```cpp
PWSTR path = nullptr;
SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path);
// path 例如 L"C:\\Users\\xixi\\AppData\\Roaming"
CoTaskMemFree(path);
```

本项目固定使用 `<%APPDATA%>\bntech\images\` 作为图片根目录、`<%APPDATA%>\bntech\images\info.json` 作为遍历索引文件、`<%APPDATA%>\bntech\config.ini` 作为语言配置。

### 2.3 目录遍历与排序稳定性

Win32 的 `FindFirstFileW / FindNextFileW` 不保证返回顺序，常见行为：

- NTFS：按文件名 lexicographic 升序，但不保证。
- FAT32：按目录写入顺序。

要让"下一张"在不同机器、不同文件系统上行为一致，必须显式排序。本项目用 `std::sort` 按 wide-string 的 `_wcsicmp`（不区分大小写）排序，避免大小写文件名混排时出现 `Image1.png > image2.png` 这种反直觉结果。

### 2.4 索引持久化的常见做法

可选方案：

- A. 写到 INI（`config.ini` 内 `last_index=3`）。
- B. 写到 JSON（`info.json` 内 `{"next_index": 3, "last_file": "image3.png"}`）。
- C. 写到注册表。
- D. 写到 DLL 同目录的临时文件。

约束：

- DS 通常被多个应用循环 load / unload，索引必须落盘。
- 写到 `C:\Windows\twain_64\bntech\` 不行：需要管理员权限。
- 写到注册表对调试和重置不友好。

合理选择是 B（`info.json`），结构简单、调试方便、可手工修改。

### 2.5 文件格式识别

FreeImage 在 `FreeImage_GetFileTypeU(path)` 时按签名判断格式，对扩展名不敏感（PNG 改成 .bmp 仍能正确识别）。但是预筛选（决定哪些文件算"候选图片"）仍按扩展名做：开发者一眼能看出哪些文件参与遍历。

支持扩展名集合：`.png` / `.jpg` / `.jpeg` / `.bmp` / `.tif` / `.tiff`，比较时统一转小写。

### 2.6 兜底图 (`TWAIN_logo.png`)

DS 通常被安装到 `C:\Windows\twain_64\bntech\bntech_virtual_scanner.ds`。在同目录放一张 `TWAIN_logo.png` 作为永远存在的兜底图。定位它的 API：

```cpp
HMODULE h = nullptr;
GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                   reinterpret_cast<LPCWSTR>(&someStaticFunc), &h);
wchar_t buf[MAX_PATH];
GetModuleFileNameW(h, buf, MAX_PATH);
// strip filename, append L"TWAIN_logo.png"
```

注意必须用模块自身的句柄 (`HINSTANCE`)，不能用 `nullptr`（那是宿主 EXE 的路径）。

### 2.7 跨进程并发

同一个用户可能同时打开多个扫描应用（XnView、Twack 等），每个进程都会把 `.ds` load 一份；它们的 `info.json` 视图可能竞争。Windows 文件系统的写入是文件级原子（小文件 + 原子 rename），但仍然可能出现两个进程读相同 next_index、各自扫描同一张图。可接受的折衷：

- 读 / 写 `info.json` 时用 `CreateFileW(...,..., GENERIC_READ|GENERIC_WRITE,..., OPEN_EXISTING / CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,...)` 加 `FILE_SHARE_READ`，写时用临时文件 + `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING)` 保证原子替换。
- 不引入跨进程互斥锁；测试场景下两个应用同时扫描概率低，且复用同一张图并不会影响功能。

## 3. 设计

### 3.1 目录与文件

```
%APPDATA%\bntech\
├── config.ini                     # 语言等用户偏好
└── images\
    ├── info.json                  # 遍历索引（next_index, last_file, total）
    ├── 001_a4_color.png
    ├── 002_a4_text.png
    ├── 010_letter_photo.jpg
    └── ...
```

兜底图：

```
<install_dir>\TWAIN_logo.png       # 与 .ds 同目录
```

### 3.2 ImageSource 组件

新增（或在 `VirtualScanner` 内部维护）一个 `ImageSource` 概念，对外暴露：

```cpp
class ImageSource {
 public:
  // 解析 %APPDATA%\bntech\images，列出候选文件并稳定排序。
  void refresh();

  // 取"下一张"。无候选时返回 fallback (TWAIN_logo.png) 的路径。
  // 推进索引并持久化。
  std::wstring acquireNext();

  // 当前候选总数；> 0 表示来自目录，0 表示用兜底图。
  size_t size() const;

  // 重置索引到 0。
  void reset();

 private:
  void loadIndex();
  void saveIndex() const;

  std::vector<std::wstring> files_;   // 绝对路径，按 _wcsicmp 排序
  size_t next_index_ = 0;
  std::wstring images_dir_;           // %APPDATA%\bntech\images
  std::wstring fallback_path_;        // <install_dir>\TWAIN_logo.png
  mutable std::mutex mutex_;
};
```

调用关系：

```
VirtualScanner::acquireImage()
        │
        └─► ImageSource::acquireNext()
                ├─► (lazy) refresh() 列目录
                ├─► (lazy) loadIndex() 读 info.json
                ├─► pick files_[next_index_ % files_.size()]
                ├─► next_index_++
                └─► saveIndex() 写 info.json (原子 rename)
        │
        └─► FreeImage_LoadU(path)  → FIBITMAP*
```

### 3.3 候选文件枚举

```cpp
void ImageSource::refresh() {
  files_.clear();
  WIN32_FIND_DATAW fd{};
  std::wstring pattern = images_dir_ + L"\\*";
  HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  do {
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
    if (!isSupportedExt(fd.cFileName)) continue;
    files_.push_back(images_dir_ + L"\\" + fd.cFileName);
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  std::sort(files_.begin(), files_.end(),
            [](const std::wstring& a, const std::wstring& b) {
              return _wcsicmp(a.c_str(), b.c_str()) < 0;
            });
}

bool isSupportedExt(const wchar_t* name) {
  static const wchar_t* kExts[] = {L".png", L".jpg", L".jpeg",
                                   L".bmp", L".tif", L".tiff"};
  const wchar_t* dot = wcsrchr(name, L'.');
  if (!dot) return false;
  for (auto e : kExts) if (_wcsicmp(dot, e) == 0) return true;
  return false;
}
```

### 3.4 info.json 格式

```json
{
  "next_index": 3,
  "last_file": "002_a4_text.png",
  "total": 12,
  "updated_at": "2026-05-26T10:21:33+08:00"
}
```

字段说明：

- `next_index`：下一次扫描要拿的文件下标（0-based）。
- `last_file`：上一次扫描使用的文件名（便于调试日志和定位）。
- `total`：上一次 `refresh()` 看到的候选数量（仅供肉眼校验）。
- `updated_at`：写入时间，调试用。

读写策略：

- 读：文件不存在或解析失败，视为 `next_index = 0`。
- 写：构造 UTF-8 字符串 → 写入 `info.json.tmp` → `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING)` 原子替换 `info.json`。
- 解析：用极简手写解析器，按 `"key"\s*:\s*<value>` 提取 `next_index` 即可，其余字段允许缺失或格式异常。

### 3.5 round-robin 与文件集合变化

`acquireNext()` 取下标的写法：

```cpp
if (files_.empty()) {
  return fallback_path_;
}
size_t idx = next_index_ % files_.size();
auto path = files_[idx];
next_index_ = (next_index_ + 1) % files_.size();
saveIndex();
return path;
```

`next_index_` 模 `files_.size()` 之后再保存，确保 `info.json` 内值始终落在 `[0, total)` 范围。即使用户在两次扫描之间删除了若干文件，最坏情况下下一次扫描会从头开始，而不会越界。

### 3.6 缓存与刷新策略

- 第一次 `acquireNext()` 内部触发 `refresh()` + `loadIndex()`。
- 之后每次 `acquireNext()` 都 `refresh()`，让用户在扫描会话之间新增 / 删除图片生效。
- `refresh()` 的代价是一次 `FindFirstFile` 遍历 + sort，对几十张图片量级开销可以忽略。
- `loadIndex()` 只在构造时调用一次；运行期 `next_index_` 由内存维护，每次 `acquireNext()` 后 `saveIndex()` 落盘。

### 3.7 兜底图位置解析

```cpp
std::wstring resolveFallbackPath() {
  HMODULE h = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                     GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCWSTR>(&resolveFallbackPath), &h);
  wchar_t buf[MAX_PATH];
  GetModuleFileNameW(h, buf, MAX_PATH);
  std::wstring p = buf;
  size_t slash = p.find_last_of(L"\\/");
  if (slash != std::wstring::npos) p.resize(slash);
  return p + L"\\TWAIN_logo.png";
}
```

不论 DS 被安装到 `C:\Windows\twain_64\bntech` 还是开发期被加载自 `build\win64\Release`，都能正确定位与自身共存的 `TWAIN_logo.png`。

### 3.8 与 VirtualScanner 的集成

```
VirtualScanner::preScanPrep(settings)
   1. image_source_.acquireNext()       → wstring path
   2. FreeImage_LoadU(detectFormat(path), path)  → FIBITMAP*
   3. ensure24BitDib()
   4. applyPageSizeScaling()
   5. applyPixelFormat()
   6. applyDpiMetadata()
   → current_fibitmap_
```

`ImageSource` 是 `VirtualScanner` 的成员，生命周期与 `VirtualScanner` 一致；外部不直接访问 `ImageSource`，只通过 `VirtualScanner::preScanPrep` 间接驱动。

## 4. 主要设计决策与原因

### 4.1 把图片源放在 `%APPDATA%\bntech\images`

- 决策：固定路径，不让用户在 UI 配置。
- 原因：测试机器多、目录变化多会让"自动化测试 + 文档"不稳。固定路径任何脚本、CI 都能复制图片到那里；同时 `%APPDATA%` 不需要管理员权限，普通用户即可操作。`config.ini`、`info.json` 都放同一根目录便于运维。

### 4.2 字母序而不是按修改时间

- 决策：按文件名 (`_wcsicmp`) 升序。
- 原因：可预期、可控、跨文件系统稳定。用户可以用 `001_`, `002_` 前缀手动控制顺序；按 mtime 排序会被 git checkout、复制粘贴等操作改变。

### 4.3 round-robin 而不是扫到尽头停止

- 决策：到末尾回绕。
- 原因：测试场景常常需要持续触发扫描（性能、稳定性测试），如果停止会让自动化脚本卡住。回绕也让用户感受"无穷的纸张"。需要重置时只需删除 `info.json`，比加 UI 按钮简单。

### 4.4 索引持久化用 `info.json`

- 决策：JSON 文件 + 原子 rename。
- 原因：人类肉眼读得懂、能手工修改 / 删除；和 `config.ini` 区分（一个偏好、一个状态）；JSON 解析比 INI 解析略复杂但适合未来扩展（添加 `last_file` / `total` / `updated_at` 字段）。

### 4.5 不引入 JSON 第三方库

- 决策：手写极简 JSON 读写。
- 原因：项目只需要解析 1 个数值字段（next_index），加 nlohmann / RapidJSON 增加构建复杂度不值得。手写 50 行内的极简实现足够。

### 4.6 目录为空时回退 `TWAIN_logo.png` 而不是返回错误

- 决策：用兜底图。
- 原因：DS 第一次被新用户安装时 `images\` 必然为空，如果直接报错应用就什么都看不到，初次使用体验差。兜底图能让用户立刻验证扫描链路通畅，再去补图。

### 4.7 兜底图与 `.ds` 同目录

- 决策：MSI 把 `TWAIN_logo.png` 一起装到 `C:\Windows\twain_64\bntech\`。
- 原因：用 `GetModuleFileNameW` 拿自身路径再拼即可，定位简单稳定；不依赖 `%APPDATA%`，新用户也保证有图。

### 4.8 每次 `acquireNext` 重新 refresh 目录

- 决策：不缓存目录列表。
- 原因：测试场景下用户经常往目录里加图。缓存会让"我刚加了 image_new.png 为什么没扫到"成为常见 bug。`FindFirstFile` 对几十张文件几乎零成本。

### 4.9 不加跨进程锁

- 决策：只用进程内 `std::mutex`，跨进程通过文件原子 rename 抢占。
- 原因：测试环境下两个应用同时扫描的概率低；即使竞争也只是两个应用扫到同一张图，无功能损坏；引入 named mutex 反而带来僵尸锁等运维负担。

### 4.10 索引按文件总数取模而不是计数到下一帧

- 决策：保存 `next_index = (next_index + 1) % files_.size()`。
- 原因：避免 `next_index` 无界增长（虽然实际不会溢出，但便于人工查看 `info.json` 时一眼看出"下一张是第几张"）。

## 5. 架构各组件改动点

### 5.1 `src/virtual_scanner.h/.cpp`

- 引入 `ImageSource` 成员（或同等内部模块），构造时确定 `images_dir_` 和 `fallback_path_`。
- 新增 / 整理：
  - `acquireImage()`：调用 `image_source_.acquireNext()` + `FreeImage_LoadU`。
  - `loadFallbackImage()`：在 `acquireNext` 返回 `fallback_path_` 时使用同一个加载路径。
- 暴露 `resetImageIndex()`（可选）作为调试/测试入口。

### 5.2 `src/twain_data_source.cpp`

- 在 `DG_CONTROL / DAT_USERINTERFACE / MSG_ENABLEDS` 触发的 `preScanPrep` 之前不做特殊处理；`VirtualScanner` 内部完成"下一张"的选择。
- 在日志中记录 `last_file`，便于调试 Native / File Transfer 链路。

### 5.3 `src/ds_entry.cpp`

- 无直接改动；只要保证 `VirtualScanner` / `ImageSource` 在 DLL 卸载时正确析构 (Stage4 关闭 DS 时)。

### 5.4 安装层 (`installer/*.wxs`)

- 把 `TWAIN_logo.png` 加入 Component，安装到 `<INSTALL_DIR>\bntech\TWAIN_logo.png`。
- 不创建 `%APPDATA%\bntech\images\`（首次扫描时按需创建即可，避免无谓写入用户目录）。

### 5.5 文档

- `README.md` 的 "Image source folder" / "准备测试图片" 章节描述目录、扩展名、重置方式。
- `docs/index.md` / 博客新增"如何替换扫描源"教程。

### 5.6 测试影响

- 单元 / 集成测试用例：
  - 空目录：扫描应得到 `TWAIN_logo.png` 的内容。
  - 1 张图片：连续扫 3 次，三次都返回同一张。
  - 3 张图片：连续扫 5 次，文件顺序应为 1 → 2 → 3 → 1 → 2，`info.json` 中 `next_index` 落点正确。
  - 删除 `info.json`：下次扫描从第一张开始。
  - 两次扫描之间新增一张：下一次扫描应能看到（`refresh` 生效）。
  - 同时打开两个应用扫描：不崩溃，不破坏 `info.json`。

## 6. 限制

- 目录路径固定为 `%APPDATA%\bntech\images`，无法通过 UI 修改（需手工操作文件系统）。
- 排序按文件名字典序，不支持自然数字序（`image10` 会排在 `image2` 之前）。如需自然序需自实现比较器。
- 不支持递归子目录；图片必须放在 `images\` 根。
- 同一会话内只取一张图，不支持 ADF 模拟"一次会话连续 N 张"。
- 索引精度只到"下一张是第几张"，不记录"已扫过哪几张"或扫描历史。
- 文件竞争策略弱：极端情况下两个应用同时扫描可能扫到同一张。
- 兜底图固定为 `TWAIN_logo.png`，无法在 UI 切换其他兜底图。
- 极大目录（> 数千张）下 `refresh()` + `sort` 的延迟会变明显（当前未做分页 / 懒列举）。
- 文件被独占打开（其他进程正在写）时，`FreeImage_LoadU` 会失败；当前实现直接报错，不重试。

## 7. 下一步工作

- settings UI 增加 "Reset image index" 按钮，调用 `VirtualScanner::resetImageIndex()`。
- settings UI 增加 "Choose image folder" 选项，把路径写入 `config.ini`；缺省仍是 `%APPDATA%\bntech\images`。
- 支持自然数字序排序（`image2.png < image10.png`），实现 `naturalCompare`。
- ADF 模拟：在一次会话内按 `feeder_count` 连续输出 N 张，`DAT_PENDINGXFERS.Count` 报告剩余张数。
- 增加 "shuffle" 模式：每次会话用伪随机顺序选图，方便压力测试。
- 增加 `info.json` 中的 `history`（最近 N 张），便于调试与回溯。
- 跨进程并发改进：用 `LockFileEx` 在 `info.json.lock` 上做短锁，避免同图重复扫描。
- 监视目录变化（`ReadDirectoryChangesW`），自动 refresh，减少手动重置场景。
- 在博客 / `docs/` 中加图文教程：怎么准备 ADF 测试集、怎么配合 `images/info.json` 做回归测试。

</details>

<details>
<summary>English</summary>

## 1. Requirements

The virtual scanner must have "paper" to feed. This project uses a local image folder as the source of virtual pages: on every scan request, the DS picks the next image from the folder and renders it with the current settings (DPI, pixel type, page size).

Functional requirements:

- Read candidate images from `%APPDATA%\bntech\images\`.
- Support common formats: PNG, JPG, JPEG, BMP, TIF, TIFF.
- Files are sorted alphabetically (case-insensitive, locale-stable); each scan advances to the next file.
- Wrap around to the first file after the last (round-robin) for long-running test loops.
- Persist the scan index across DLL unload / reload.
- When the folder is missing, empty, or contains only unsupported files, fall back to the bundled `TWAIN_logo.png` from the DS install directory — never fail the scan.
- Resetting progress must be trivial: delete one file (`info.json`), no UI step required.
- Users may add or remove images between scans; the next scan must reflect the new state.
- Thread-safety: UI thread, TWAIN dispatcher thread, and any strip-copy work must coexist safely.
- ADF expansion (future): support emitting N images per session, still in alphabetical order.

Non-functional requirements:

- Index file format must be human-readable and easy to edit.
- No third-party JSON dependency; a tiny hand-written parser suffices.
- Image loading stays on FreeImage.
- Ordering must not depend on filesystem timestamps.

## 2. Domain knowledge

### 2.1 TWAIN scan session and "next image"

A simplified TWAIN session looks like:

```
OpenDS → EnableDS → (XferReady) → DAT_IMAGEINFO → DAT_IMAGE{NATIVE|FILE}XFER → DAT_PENDINGXFERS → DisableDS → CloseDS
```

`DAT_PENDINGXFERS.Count` declares how many more images the scanner has ready. For a flatbed simulation this is `0` (one image per session). "Next image" means: each new `EnableDS` session takes the next item from the folder.

### 2.2 `%APPDATA%` and user-scoped data

`%APPDATA%` resolves to `C:\Users\<user>\AppData\Roaming`. Suitable for per-user data because:

- No admin rights required.
- Per-user isolation.
- MSI uninstall does not remove user data.

Resolve via:

```cpp
PWSTR p = nullptr;
SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p);
CoTaskMemFree(p);
```

The project pins `<%APPDATA%>\bntech\images\` for images, `info.json` for the index, and `config.ini` for language.

### 2.3 Directory enumeration and sort stability

`FindFirstFileW / FindNextFileW` does not guarantee order. The project sorts explicitly with `_wcsicmp` (case-insensitive) so behavior is identical across NTFS / FAT32 / SMB shares and uppercase / lowercase filenames.

### 2.4 Index persistence options

Choices considered:

- INI (`last_index=3`).
- JSON (`{"next_index": 3, ...}`).
- Registry.
- File next to the DLL.

Constraints: DLL is loaded/unloaded per application, so the index must persist on disk; the DLL directory (`C:\Windows\twain_64\bntech\`) requires admin write; the registry is opaque. JSON in `%APPDATA%\bntech\images\info.json` wins on simplicity and debuggability.

### 2.5 Format detection

`FreeImage_GetFileTypeU` detects format by signature so extension renames still work. But the pre-filter (which files qualify as candidates) uses extensions for human transparency: `.png / .jpg / .jpeg / .bmp / .tif / .tiff` (case-insensitive).

### 2.6 Fallback image (`TWAIN_logo.png`)

The MSI installs `TWAIN_logo.png` next to the `.ds` so a permanent fallback always exists. Locate the DLL's own directory through `GetModuleHandleExW` + `GetModuleFileNameW` using a function pointer inside the module (NOT `nullptr`, which resolves the host EXE path).

### 2.7 Cross-process concurrency

Multiple TWAIN applications can load the DS concurrently. The project relies on atomic file rename for `info.json` updates and does not add cross-process mutexes; the worst case is two apps scanning the same image, which is acceptable in a test scanner.

## 3. Design

### 3.1 Layout

```
%APPDATA%\bntech\
├── config.ini
└── images\
    ├── info.json
    ├── 001_a4_color.png
    ├── 002_a4_text.png
    └── ...
```

Fallback:

```
<install_dir>\TWAIN_logo.png
```

### 3.2 ImageSource component

```cpp
class ImageSource {
 public:
  void refresh();
  std::wstring acquireNext();   // wrap-around; returns fallback if empty
  size_t size() const;
  void reset();
 private:
  void loadIndex();
  void saveIndex() const;

  std::vector<std::wstring> files_;
  size_t next_index_ = 0;
  std::wstring images_dir_;
  std::wstring fallback_path_;
  mutable std::mutex mutex_;
};
```

Used by `VirtualScanner::preScanPrep` → `acquireImage`.

### 3.3 Enumeration

```cpp
void ImageSource::refresh() {
  files_.clear();
  WIN32_FIND_DATAW fd{};
  HANDLE h = FindFirstFileW((images_dir_ + L"\\*").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  do {
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
    if (!isSupportedExt(fd.cFileName)) continue;
    files_.push_back(images_dir_ + L"\\" + fd.cFileName);
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  std::sort(files_.begin(), files_.end(),
            [](const std::wstring& a, const std::wstring& b) {
              return _wcsicmp(a.c_str(), b.c_str()) < 0;
            });
}
```

### 3.4 info.json format

```json
{
  "next_index": 3,
  "last_file": "002_a4_text.png",
  "total": 12,
  "updated_at": "2026-05-26T10:21:33+08:00"
}
```

- `next_index`: index to use on the next scan (0-based).
- `last_file`: name used on the previous scan (for logs).
- `total`: candidate count seen at last `refresh()`.
- `updated_at`: write timestamp.

Read: parse leniently; missing or malformed → treat as `next_index = 0`.
Write: serialize → write `info.json.tmp` → `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING)` for atomic replacement.

### 3.5 Round-robin & set changes

```cpp
if (files_.empty()) return fallback_path_;
size_t idx = next_index_ % files_.size();
auto path = files_[idx];
next_index_ = (next_index_ + 1) % files_.size();
saveIndex();
return path;
```

Modulo keeps the persisted index inside `[0, total)`.

### 3.6 Caching policy

- `loadIndex()` runs once at construction.
- `refresh()` runs on every `acquireNext()` so additions / removals between scans take effect.
- `saveIndex()` runs after every successful `acquireNext()`.

### 3.7 Fallback path resolution

```cpp
HMODULE h = nullptr;
GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                   reinterpret_cast<LPCWSTR>(&resolveFallbackPath), &h);
wchar_t buf[MAX_PATH];
GetModuleFileNameW(h, buf, MAX_PATH);
```

Works both for installed DS (`C:\Windows\twain_64\bntech`) and dev builds (`build\win64\Release`).

### 3.8 VirtualScanner integration

```
preScanPrep(settings):
  path = image_source_.acquireNext();
  FIBITMAP* bmp = FreeImage_LoadU(detectFormat(path), path);
  ensure24BitDib() → applyPageSizeScaling() → applyPixelFormat() → applyDpiMetadata()
  current_fibitmap_ = bmp;
```

## 4. Key decisions and rationale

### 4.1 Hard-coded `%APPDATA%\bntech\images`

- Decision: no UI configuration for the source folder.
- Rationale: Stable for automation, docs, and CI; `%APPDATA%` is per-user and admin-free; co-locates with `config.ini` and `info.json`.

### 4.2 Alphabetical order, not mtime

- Decision: `_wcsicmp` ascending.
- Rationale: Predictable; users can prefix filenames (`001_`, `002_`) to control order; mtime would be perturbed by git operations or copy/paste.

### 4.3 Round-robin instead of stopping at end

- Decision: wrap.
- Rationale: Long-running test loops never stall; reset is "delete `info.json`".

### 4.4 JSON file for index

- Decision: `info.json` + atomic rename.
- Rationale: Human-readable; separates state from preferences (`config.ini`); extensible to richer fields.

### 4.5 No third-party JSON library

- Decision: hand-written tiny parser.
- Rationale: One numeric field is enough; saves build complexity.

### 4.6 Fallback image when folder is empty

- Decision: `TWAIN_logo.png` instead of error.
- Rationale: First-run users have an empty folder; failing the scan would be a poor first impression.

### 4.7 Fallback bundled next to `.ds`

- Decision: MSI installs `TWAIN_logo.png` next to the binary.
- Rationale: Resolvable via `GetModuleFileNameW`; not dependent on `%APPDATA%`.

### 4.8 Refresh on every `acquireNext`

- Decision: no directory cache between scans.
- Rationale: Newly added files must appear immediately; `FindFirstFile` is cheap.

### 4.9 No cross-process locking

- Decision: only intra-process `std::mutex`, plus atomic rename for `info.json`.
- Rationale: Acceptable trade-off; named mutexes risk orphans without meaningful benefit for a test scanner.

### 4.10 Modulo index by `files_.size()` on save

- Decision: persist `(next + 1) % size`.
- Rationale: Keeps `info.json` easy to interpret at a glance.

## 5. Architectural component changes

### 5.1 `src/virtual_scanner.h/.cpp`

- Introduce `ImageSource` member with `images_dir_` and `fallback_path_`.
- `acquireImage()` calls `image_source_.acquireNext()` + `FreeImage_LoadU`.
- Optional `resetImageIndex()` for testing.

### 5.2 `src/twain_data_source.cpp`

- No special branching for image selection; rely on `VirtualScanner`.
- Log `last_file` for diagnostics.

### 5.3 `src/ds_entry.cpp`

- No direct changes; ensure clean destruction on DLL unload.

### 5.4 Installer (`installer/*.wxs`)

- Add `TWAIN_logo.png` to a component installed alongside the `.ds`.
- Do NOT create `%APPDATA%\bntech\images` at install time; create on demand.

### 5.5 Documentation

- README "Image source folder" describes folder, extensions, and reset.
- New blog/post about replacing the scan source.

### 5.6 Test impact

- Empty folder → `TWAIN_logo.png` returned.
- 1 image, 3 scans → all return the same image.
- 3 images, 5 scans → 1, 2, 3, 1, 2; `next_index` lands correctly.
- Delete `info.json` → next scan starts from 0.
- Add a file between scans → it appears next round.
- Two concurrent applications → no crash; `info.json` stays well-formed.

## 6. Limitations

- Folder path is hard-coded; no UI override.
- Lexicographic only; `image10` sorts before `image2`. No natural ordering.
- No recursion into subdirectories.
- One image per session; no ADF batch yet.
- Index granularity is "next index" only; no per-file history.
- Weak cross-process arbitration; two apps may pick the same file.
- Fallback is fixed at `TWAIN_logo.png`; not user-replaceable through UI.
- Very large folders (thousands of files) make `refresh()` + sort noticeable.
- Exclusively-opened files cause `FreeImage_LoadU` to fail without retry.

## 7. Next steps

- Settings UI "Reset image index" button calling `VirtualScanner::resetImageIndex()`.
- Settings UI "Choose image folder" persisted in `config.ini`.
- Natural-order sort (`image2 < image10`).
- ADF simulation: emit N images per session and report `DAT_PENDINGXFERS.Count` accordingly.
- Shuffle mode (pseudo-random order) for stress testing.
- Add `history` array to `info.json` for traceability.
- `LockFileEx` on `info.json.lock` to better coordinate concurrent applications.
- `ReadDirectoryChangesW` watcher for live refresh.
- Tutorial in `docs/` / blog: preparing ADF test sets and regression suites that pair with `info.json`.

</details>
