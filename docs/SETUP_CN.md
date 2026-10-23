# Muse Charm · Mosaico 手机配置与配对图文教程

本教程适用于**已安装本项目新版千问固件**的 ESP-Mosaico 裸机。手机即可完成服务配置、Wi-Fi 联网和 Muse 账号配对。尚未安装固件，请先看 [项目首页的安装步骤](../README.md#直接安装)。

**操作顺序：连接设备热点 → 访问 192.168.4.1 → 填 Muse token 和千问 API Key → 保存重启 → Muse App 添加设备 → 按红色 AI 键确认 → 输入常用 Wi-Fi 密码 → 配对成功。**

[观看实际设备演示视频](assets/demo/muse-charm-demo.mp4)（约 39 秒，含声音；如果 GitHub 没有显示播放器，可下载后播放）。

## 开始前准备

- 已安装固件、有足够电量的设备；不需要摄像头或其他扩展插件。
- 已登录 Muse 账号的 Muse 手机 App。
- 自己的 **Muse Gadget SDK token** 和 **阿里百炼 API Key（北京地域）**。
- 常用 Wi-Fi 名称和密码。

**建议先在有互联网的网络下获取两个凭据，准备好复制，再连接设备热点。**设备热点只提供本地配置，不能用来访问云平台。两个凭据不能互换，也不要公开到截图、聊天或 GitHub。

| 网络 | 用途 |
| --- | --- |
| `Mochi-Setup-XXXX` | 手机访问设备配置后台，无密码 |
| 家中或办公室常用 Wi-Fi | 设备长期访问 Muse 与千问，需要该网络密码 |

## 1. 手机连接设备 Wi-Fi

设备首次开机、尚未保存服务配置时，会开启 **`Mochi-Setup-XXXX`** 热点，末尾字符因设备而异。

打开手机 **设置 → Wi-Fi**，选择这个热点，**无需密码**。出现选中标记后继续。

<img src="assets/setup/01-connect-hotspot.png" width="440" alt="手机连接设备的免密码配置热点，编号已遮挡">

「不安全的网络」表示热点未设密码；「无互联网连接」不影响本地配置。如手机询问是否继续使用，选择保持连接。配置完成后热点会关闭。

## 2. 浏览器打开 192.168.4.1

在 Safari 或 Chrome 的**地址栏**输入：

```text
http://192.168.4.1
```

手机必须仍连接设备热点。使用 `http://`，不要改为 `https://`，也不要输入搜索框。进入「首次配置 / First-time setup」页面，右上角可切换 **中文 / English**。

<img src="assets/setup/02-muse-token.png" width="440" alt="首次配置页的语言切换和 Muse token 输入框">

图中保留语言按钮与 Muse token 部分；下面的朗读服务统一按本教程的**新版千问配置**填写。

## 3. 获取并填写 Muse token

你提供的平台入口是 [Muse 平台](https://muse.ai/platform)。目前官方 SDK README 直接指向 **[Muse Gadget SDK tokens](https://gadgets.muse.ai/settings/sdk-tokens)**，获取设备 token 可直接访问这个页面。

1. 在可上网的网络下打开 SDK tokens 页面，按提示登录自己的 Muse 账号。
2. 在页面中获取自己的 Gadget SDK token；登录后的具体按钮以当前页面为准。
3. 返回设备热点，重新打开 `http://192.168.4.1`。
4. 把完整 token 粘贴到 **Muse token**，检查首尾没有空格或换行。

这里需要 **Gadget SDK token**，不是邀请码、账号额度词元数量或 App 登录密码。token 的用途见 [Muse Gadget SDK 官方说明](https://github.com/facebookincubator/muse-gadget-sdk)。

## 4. 获取阿里千问 TTS API Key

千问负责把 Muse 的文字回复朗读出来，Muse 仍负责对话。打开以下网址获取语音服务凭据：

- **[百炼 API Key 管理（北京）](https://bailian.console.aliyun.com/cn-beijing/model/settings/api-key)**
- [阿里云百炼控制台](https://bailian.console.aliyun.com/)
- [阿里云官方获取说明](https://help.aliyun.com/zh/model-studio/get-api-key/)

登录自己的阿里云账号，按提示开通百炼服务，确认地域为 **华北 2（北京）**。进入 API Key 管理，已有可用 Key 可以复制；也可点击「创建 API Key」，按页面要求选择归属账号和业务空间后创建、复制。不同地域的 Key 与接口不能混用。具体流程参见上面的官方说明。

使用能够调用千问语音模型的百炼 API Key；千问网页的登录密码不能填在这里。免费额度、充值与调用费用以账号控制台实时信息为准。

## 5. 填写新版千问朗读配置

回到设备后台，在「朗读服务 / Speech service」选择 **`Qwen · 阿里百炼`**。新版固件会填入默认地址、模型和音色。把自己的百炼 API Key 填入 **TTS token**，核对下表：

| 页面字段 | 应填写的值 |
| --- | --- |
| 服务提供方 / Provider | `Qwen · 阿里百炼` |
| TTS API 地址 / TTS API URL | `wss://dashscope.aliyuncs.com/api-ws/v1/realtime` |
| TTS token | **你自己的北京地域百炼 API Key** |
| 模型名称 / Model name | `qwen3-tts-flash-realtime` |
| 音色名称 / Voice name | `Cherry` |
| 语音接口类型 / Speech API type | 选择 **PCM 16 kHz** 的千问选项 |

<img src="assets/setup/15-qwen-config.png" width="440" alt="新版固件千问配置页：Qwen、实时接口、模型与 Cherry 音色，凭据为空">

上图是仓库现有新版配置页面的浏览器预览，两个凭据框均为空；地址较长，在手机输入框内可能显示不全，请按表格核对完整值。

保持地址与名称完整、大小写一致。这里使用实时语音 **WSS** 接口，不要填写聊天模型的 `/compatible-mode/v1` 地址或只填域名。Muse token 与 TTS token 两个框都需要填。

本教程只提供新版千问配置。如果没有 `Qwen · 阿里百炼` 选项，请先升级本项目的千问固件，再配置。

## 6. 保存并重启

检查填写内容后，点击 **「保存并重启 / Save and restart」**。默认不勾选「显示 token」，即可隐藏凭据。

<img src="assets/setup/03-save-restart.png" width="440" alt="保存并重启按钮与保存成功提示">

出现保存成功提示后，设备约两秒自动重启，配置热点关闭。手机断开这个热点属于正常现象。将手机切回常用 Wi-Fi 或可上网的网络，再打开 Muse App。

**此时仅完成服务参数保存，还要在 App 中完成联网与账号配对。**热点页面只能检查格式，无法确认云端 Key 是否有效，最终需用实际对话和朗读验证。

## 7. Muse App 进入「设备」，开启开发者模式

在 Muse App 打开 **设置 → 设备**。

<img src="assets/setup/04-app-settings.png" width="440" alt="Muse App 设置中的设备入口">

在「设备」页开启 **Developer mode（开发者模式）**，允许添加社区设备。这是 [Muse 官方 SDK](https://github.com/facebookincubator/muse-gadget-sdk) 的配对要求。

<img src="assets/setup/06-developer-mode.png" width="440" alt="开启 Developer mode">

手机如请求蓝牙、附近设备或本地网络权限，请按 App 提示允许设备发现与连接，并把手机靠近设备。

## 8. 点加号，扫描并连接设备

点击「设备」页面右上角 **`＋`**。

<img src="assets/setup/05-add-device.png" width="440" alt="设备页右上角加号">

找到 **`MuseGadget-……`**，点击右侧 **「连接」**。它是 App 中的设备名，与配置用的 `Mochi-Setup-XXXX` 热点名不同。

<img src="assets/setup/07-discover-device.png" width="440" alt="扫描到 MuseGadget 后点击连接，编号已遮挡">

未找到时，确认设备已完成重启，再点「再次扫描」。

## 9. 按顶部红色 AI 键确认

App 显示「按下设备按钮即可确认」时，**短按一次设备顶部的红色 AI 键**，确认你正在配对的设备。此处无需按住说话。

<img src="assets/setup/08-pair-confirm.png" width="440" alt="App 提示按设备按钮确认">

<img src="assets/setup/09-red-ai-button.png" width="440" alt="按下设备顶部红色 AI 键">

确认后按 App 提示点「继续」或进入下一步。正常对话时按住该键录音，配对提示出现时短按确认，两种操作请区分。

## 10. 阅读社区设备授权提示

App 可能提示设备会访问你的 Muse 助手，并可能通过助手读取、发送消息或执行操作。请阅读提示，确认是自己的设备及可信固件后，选择 **「Continue anyway」** 继续；「Exit to safety」用于取消配对。

<img src="assets/setup/10-pair-permission.png" width="440" alt="社区设备授权提示，助手名称已遮挡">

授权提示与 Wi-Fi 设置的先后顺序可能随 App 版本变化，以实际屏幕为准。

## 11. 输入常用 Wi-Fi 密码，完成配对

在 App 后续 Wi-Fi 设置中，选择**设备长期使用**的网络，输入该网络密码并提交。这里填的是家中或办公室网络的密码，不是免密码配置热点。

等待连接与配对完成，保持设备供电、手机在附近。提供的照片没有 Wi-Fi 密码输入页，这一步以 App 当前表单为准。

完成后，App「其他设备」中应出现 `MuseGadget-……`：

<img src="assets/setup/11-paired-device.png" width="440" alt="配对后的设备出现在列表，编号已遮挡">

设备主界面左上角出现 **「Muse 在线」**。打开齿轮设置，底部应显示 **「网络已连接 · Muse 已配对」**。

<img src="assets/setup/12-muse-online.png" width="440" alt="设备显示 Muse 在线与初始角色">

App 中有设备记录与设备当前在线是两项检查；只有列表记录时，还需检查联网状态。

## 12. 第一次对话与常用设置

按住顶部红色 AI 键，说「你好，简单介绍一下你自己」，然后松开发送。设备应显示 Muse 文字回复，并由千问朗读。点击文字框可展开，上下滑动查看全文；点击框外收起。

主页喇叭图标可以一键静音，再点一次恢复朗读；齿轮进入设置。

| 设置项 | 操作与作用 |
| --- | --- |
| 音量、亮度 | 拖动滑块，左侧百分比实时变化 |
| 语言 | 点击切换中文 / English |
| 自动熄屏 | 滑块可选 5、10、15、20、30、60 秒 |
| 语速 | 拖动滑块，影响后续朗读 |
| 自动翻页 | 拖动滑块设置间隔，默认 7 秒 |
| 手机配置 / Phone setup | 重新开启配置热点，更换服务凭据 |

<img src="assets/setup/13-settings-zh.png" width="360" alt="中文设置页">
<img src="assets/setup/14-settings-en.png" width="360" alt="English 设置页">

照片中的 30 秒熄屏、1.10 倍语速、6 秒翻页等是拍摄时的示例设置，不代表默认值。无操作后屏幕逐渐变暗、熄屏，可轻触屏幕或按键唤醒。

## 以后如何修改 token

在设备上点 **齿轮 → 手机配置 / Phone setup**，设备重启并再次开启配置热点。手机连接后访问 `http://192.168.4.1`，重新完整填写服务信息，保存重启。

页面不回显已保存的 token，请提前准备两个凭据。此入口保留已有 Muse 配对记录；已配对设备通常无需重复添加。未提交新配置时，打开配置页不会覆盖旧凭据。

## 常见问题

**看不到热点**：首次未配置时自动开启；已配置设备先点「手机配置」。确认设备开机并完成重启。

**192.168.4.1 打不开**：确认仍连接设备热点，在地址栏输入完整 `http://192.168.4.1`。手机自动切回其他 Wi-Fi 时重新连接；必要时临时关闭影响本地访问的 VPN。

**保存失败**：检查必填字段与 token 首尾空格、换行，保持连接热点后重试。未显示保存成功前不要主动切换网络。

**App 扫描不到**：确认设备已重启，开启 Developer mode，按 App 提示检查发现权限，靠近后重新扫描。

**按键没有完成配对**：在 App 已出现确认提示时短按顶部红色键。正常对话则按住说话、松开发送。

**有文字没有朗读**：确认喇叭未静音、音量非 0，再核对北京地域 API Key、账户额度和语音模型权限，以及完整 WSS 地址、模型、音色。修改并重启后用一条新对话测试。

**设备未显示 Muse 在线**：检查常用 Wi-Fi 密码、互联网连接、Muse token，以及 App 配对是否完成。只保存后台参数不等于联网成功。

## 图片与视频说明

图片来自真实设备照片与 App 截图，已裁切重点；私人 Wi-Fi 名称、个人设备名称、设备编号和助手名称已遮挡或裁出，并移除拍摄元数据。新版千问配置图单独标注为现有固件页面的浏览器预览。没有展示真实 token，也没有为未提供的 App 页面编造截图。视频保留声音与操作顺序，遮挡背景顶部名称区域，移除定位等拍摄元数据后转为 MP4。
