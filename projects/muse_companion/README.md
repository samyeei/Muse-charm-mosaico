# Muse 伙伴

ESP-Mosaico 裸机上的语音 AI 伙伴，使用 Muse Gadget SDK、GSP 1.5.1 和可配置的 TTS。纯黑背景，提供待机、聆听、思考、说话、困倦、错误、开心、开机八种动态。

- 上方 AI 键按住说话，松开发送；再次按住可打断朗读。
- 默认朗读；首页喇叭按钮一键静音，保留文字与录音能力。
- 左上角显示 Muse 连接状态，右上角为圆角电池图案；从屏幕顶部下拉打开设置，上滑返回，待机角色呼吸，轻触角色触发四秒开心跳跃和爱心，开机招呼。毛绒版复用原图集作位置动画，表情使用对应姿态，不是逐帧口型或眨眼动画。
- 点击回复框展开，文字左对齐，可上下滑动阅读全文；点击框外收起，框内点击保持展开。
- 设置页支持中英文、音量、亮度、熄屏时间、朗读语速和翻页时间，显示连接与配对状态；偏好保存在本机。
- 语言通过按钮切换；音量、亮度、熄屏、语速和翻页均通过横向滑块调节，左侧数值随拖动实时更新，松手保存。语速支持 0.75–1.50 倍，下一次朗读生效；语言切换不翻译历史回复。
- 熄屏可选 5/10/15/20/30/60 秒，默认 15 秒，最后两秒渐暗；录音、等待和朗读保持亮屏，触摸、AI 键或主板运动唤醒。
- 第一版不依赖扩展插件。自动环境光亮度等待光线传感器扩展接入。

## 二代迁移候选

新版实机验证尚未完成；发布状态以仓库根目录的说明为准。

## 一代历史配置（2026-10-05）

恢复奶白色初始角色与八种位置动画。首次配置热点免密码，保存后关闭。手机配置默认预设为阿里百炼 `qwen3-tts-flash-realtime` / `Cherry`，使用 WSS 实时接口，PCM16/16kHz，无额外数字增益。硅基流动和 OpenAI 兼容接口仍可选择，token、配对及设置保留。自定义服务不变，手机后续保存的选择优先。

## 配对与使用

在 Muse 手机 App 中添加设备，蓝牙名为 `MuseGadget-` 加设备 MAC 后六位。按 App 提示配网与配对；需要确认时短按上方 AI 键。完成后长按该键提问，松开等待文字回答与朗读。

首次启动且未保存服务配置时，设备开启无需密码的 `Mochi-Setup-XXXX` 热点，屏幕显示“无需密码，直接连接”。手机连接该热点，保留无互联网连接，在浏览器打开 `http://192.168.4.1`。页面支持中英文，填写 Muse token 及 TTS API 完整 HTTPS/WSS 地址、token、模型和音色；默认阿里千问，也有硅基流动、OpenAI 预设和自定义兼容服务。保存后设备自动重启并关闭热点，随后通过 Muse App 配网和账号配对。

配置存入独立 NVS 命名空间 `companion_keys`，固件不再引用 `main/local_secrets.h`；旧凭据文件不删除，旧备份包仍需保持私有。页面不回显已保存的 token，不在浏览器保存 token。TTS 支持千问 WebSocket 实时 PCM16/16kHz、硅基流动兼容 PCM16/16kHz 和 OpenAI 兼容 PCM16/24kHz 接口；24kHz 转为设备 16kHz 播放。其他响应格式或协议需要单独适配，并非任意模型都能直接使用。

更新保留 Muse 配对、设备身份和偏好。仅升级尚未使用手机配置的旧版时需输入一次服务配置；已经手机配置的设备无需重填，已有配对可继续使用，无需清除 NVS。配置完整后不开启设置热点。需要修改配置时在设置中点击“手机配置”，重启进入热点配置模式，重新填写全部服务字段。详细使用与验收见[手机首次配置](../../docs/muse-companion-setup_CN.md)。更新操作使用工作区 `mosaico.py`，保留 Recovery，禁止 SDK 自行写固件。

## 开发与验证

遵循[工作区工程指南](../../docs/project-init_CN.md)，使用固定 ESP-IDF 提交与 `esp32s31` 目标。生成界面后通过公共入口预览：

```powershell
python projects/muse_companion/tools/prepare_ui.py
$env:GSP_SIM_DYNAMIC_FONT='D:/AI Job/mosaico/projects/muse_companion/ui/fonts/NotoSansSC-Regular.ttf'
python mosaico.py project sim --project projects/muse_companion --interactive
```

PC 预览运行同一份 C 界面与状态模型，网络、录音和朗读由模拟服务提供。它不能证明真实 Muse 对话或扬声器声音。

首次安装或修改资源使用：

```powershell
python mosaico.py iris system-update --project projects/muse_companion
```

当前 Windows 环境曾出现 Recovery 验证记录保存失败。若正常应用在线、已有 Recovery 已验证，但更新被本地记录缺失挡住，可通过公共入口进入已有 Recovery，再确认实时状态后更新；不要重复刷 Recovery：

```powershell
python mosaico.py iris rpc --project projects/muse_companion 32767 2
python mosaico.py iris list --project projects/muse_companion --details --json
```

必须确认同一设备已在线进入 recovery 模式。随后根据是否改动资源选择 system-update 或 app-update；`--skip-build` 仅用于完整且最新的构建。

普通应用 build 不会重做 `system-update-bundle`。界面资源修改后运行上面的完整 `system-update` 命令，让它生成最新应用和资源安装包；不能仅凭普通 build 成功就使用旧包的 `--skip-build`。

`main/companion_model.c` 管理交互状态；`companion_ui.c` 渲染 GSP；`companion_runtime.c` 管理主板、Muse 与消息队列；`companion_tts.c` 管理朗读。Muse 上游版本与适配说明见 [UPSTREAM.md](components/muse_link_sdk/UPSTREAM.md)。

原图集由参考奶白色连帽玩偶创作的原创角色组成，保留供恢复。Muse 生成的角色及来源记录位于 `characters/muse-generated/`，未使用 Muse 官方角色素材。中文字体来自 Google Fonts Noto Sans SC，遵循 `ui/fonts/OFL.txt`。

[已确认设计](../../docs/muse-companion-v1-design_CN.md) · [验证记录与待验项](../../docs/muse-companion-v1-acceptance_CN.md)

[Muse 角色与动画生成](../../docs/muse-companion-character-generation_CN.md)：默认延续奶白色连帽角色，也支持任意角色描述。生成、预览和更新需要连接电脑；日常动画播放独立运行。

## 小爱音箱局域网文字桥接

`main/companion_bridge.c` 在已有 Wi-Fi 连接后启动 HTTP 服务，端口 4397。桥接密钥存入独立的 `xiaoai_bridge` NVS，不改 Muse 配对和 TTS 凭据。请求使用 `Authorization: Bearer <key>`。

- `GET /health`：桥接就绪、空闲、IP 和本次启动标识。
- `POST /requests`：JSON `{ "request_id": "唯一ID", "text": "问题" }`，异步返回 queued。
- `GET /requests/<id>`：查询 queued/running/completed/failed/cancelled，完成后包含 reply。
- `POST /requests/<id>/cancel`：无请求体，取消排队/正在运行的请求。

同 ID、相同文字不重复调用 Muse；同 ID 不同文字返回 409。最多同时执行一个云端请求，保留最多 16 条结果、5 分钟 TTL，重启即清空。请求最多 2048 UTF-8 字节，回复沿用 4096 字节 SDK 缓冲。排队 30 秒、执行 90 秒超时。本机 AI 键优先取消远程请求，远程回复不进入设备 UI 或本机朗读。

全部 Muse 调用仍由原有 voice worker 持有；HTTP 任务只处理状态与有界数据。请求缓冲及 HTTP 任务栈使用 PSRAM，NVS 初始化在内部栈 voice worker 上执行。维护 RPC 服务 `0x7e15` 方法 1 返回非私密状态，方法 2 为受信任维护端导出桥接密钥，禁止写入公开日志。

群晖 Docker 桥接、配置导出、实机 API 验收脚本及部署指南位于相邻工作区 `D:/AI Job/xiaoai`。完整 LX06 对话验收还需要部署该容器并接入小爱客户端；本机模拟或构建不代表音箱实测通过。现有 SDK 共享云端上下文，不提供单独的桥接会话隔离。
