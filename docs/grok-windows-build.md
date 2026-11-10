# 64Gram Grok 员工 — Windows 测试包

这一版不需要你自己申请 Telegram `api_id`。GitHub Actions 工作流 **Windows Grok build**（`.github/workflows/win-grok.yml`）在没有密钥时，直接使用本仓库已经公开的 open credentials，和 `.github/workflows/win.yml` 里 nightly 用的是同一套：

- api_id：`611335`
- api_hash：`d524b414d21f4d37f08684c1df41ac9c`

源码里把它记为 `SnapApiId`（`Telegram/SourceFiles/api/api_authorizations.cpp`），活跃会话里会显示成 Telegram Desktop。用它打出来的程序可以登录。它不是你的私有凭证，和所有使用这套号的客户端共享 Telegram 的接口配额。

仓库里还有一套测试号 `17349` / `344583e45741c457fe1862106095a5eb`（`TDESKTOP_API_TEST`，以及 `config.h` 里的占位）。官方说明用这套号发布的客户端登录会报内部错误，所以测试包不用它。

## 以后换成你自己的

1. 打开仓库 **Settings → Secrets and variables → Actions → New repository secret**。
2. 新建两个 secret，名字必须成对：
   - `API_ID` 和 `API_HASH`
   - 或者 `TELEGRAM_API_ID` 和 `TELEGRAM_API_HASH`
3. 再跑一次 **Windows Grok build**。

两个都填了才会覆盖默认的 `611335`。只填一个，构建会失败，避免 id 和 hash 配错。凭证是编译进 `Telegram.exe` 的，换号必须重新打包。

xAI 的密钥不要放进 GitHub。装好之后，在面板右上角齿轮里填写，保存在本机 `tdata/grok_employees.json`（明文，和账号数据放在一起）。

## 怎么打出 exe

1. 打开 **Actions → Windows Grok build → Run workflow**，分支选带这次改动的分支。
2. 推送到 `cursor/**` 分支时也会自动跑。
3. 跑完后，在该次运行的 **Artifacts** 里下载 `64Gram-Grok-win-x64`。
4. 解压后运行 `Telegram.exe`。这是便携包，不是安装向导。自动更新已关掉，避免被官方包换掉。

第一次会从源码编译 Qt 等依赖，可能接近 Actions 的 6 小时上限。第二次会用缓存，快很多。构建机是 GitHub 的 `windows-latest`，x64 Debug。库是 `skip-release` 编出来的 Debug 库，所以客户端也编 Debug，和上游 Windows CI 一样。不需要 Depot runner。

`cursor/**` 上的推送和 PR 会跳过原来的 Windows 大矩阵（`.github/workflows/win.yml`），免得同时开 8 个容易失败的 Depot / ARM 任务。那个大矩阵如果手动在其他分支上跑，同样是：有 `API_ID` + `API_HASH` 就用你的，否则用上面的 open credentials。
