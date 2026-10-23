# Muse Charm 2.0 迁移候选

本分支保留初始奶白毛绒角色，并接入 Vibe Mode 0.2 / Recovery ABI 2。
当前是迁移候选，尚未通过新版真机验收，不替代 main 分支已验证的一代固件。

## 本地已通过

- ESP32-S31 应用和定制 Vibe Mode 构建；固定 ESP-IDF 提交见 DEPENDENCIES.json。
- 官方 `esp-iris-system-update/0.2` 制品和产品分区校验，应用版本 2.0.0。
- GSP 1.5.1 / gspc 0.6.1 模拟器：初始角色、静音、下拉设置、上滑返回、
  展开对话框、框内点击保持展开、上下滚动。
- 本次 18 项应用测试和 12 项 Vibe Mode 故障测试通过，覆盖任务、包校验、事务记录、备份恢复、写入中断和 ABI 2 启动意图；此前另有 NAND 容量变化验证。

## 保留的功能

按住 AI 键录音、松开发送，Muse 文本回复及千问朗读；中英文配置；音量、亮度、
语速、熄屏时间；手机热点配置；完整的八状态角色接口。

右上角为静音按钮。从屏幕顶部下拉进入设置，上滑退出。点击对话框展开，
展开后左对齐显示并可上下滑动阅读；仅点击框外还原。换装下载期间显示等待状态。

角色任务通过 CHARACTER_PACK / CHARACTER_QUERY / CHARACTER_STATUS 交换。
资源先下载至 NAND、验证完整性及兼容性，再由定制 Vibe Mode 执行写入；
保留初始角色备份和独立的上一个角色备份。挂载失败不会自动格式化。

## 安装前须知

旧 ABI 1 设备不能直接用二代包跨代更新。先按官方 0.2 迁移流程备份、释放旧
Gateway，再用本仓库配套定制 Vibe Mode 经 ROM 迁移，最后安装应用完整包。
普通官方 Vibe Mode 没有 Muse 的角色事务处理模块，不能代替定制版本验证换装。
不要整片擦除 Flash，不要清空 NAND，不要将旧协议包仅修改 schema 后使用。

`tools/bootstrap.py` 初始化固定提交并应用经过 SHA-256 检查的
`tools/patches/muse-vibe-0.2.patch`。重复执行不会重复应用补丁。
旧依赖中有冲突修改时会停止，不会重置用户文件。

候选应用包位于 `firmware/candidates/`；在真机迁移、配对、对话、TTS、角色更换和
恢复验证完成前，不建议普通用户安装，也不提交为 mosaico.ai 的稳定版本。

角色接口候选标识：`muse-ui-v5-gsp1.5.1-gspc0.6.1-bd2b06c6a30dacae`。
这是新版运行时的兼容边界，旧 v4 角色包应使用新打包工具重新生成，不能手改版本号。
`compatibility_frozen` 在验收前保持 `false`。

后续更新以 [GitHub 仓库](https://github.com/samyeei/Muse-charm-mosaico) 为准。

## 2026-10-23 真机进展

同一台设备已完成 ROM 迁移至定制 Vibe Mode 0.2，再通过正式 system-update
安装 2.0.0 应用。应用健康检查、ELF 校验和初始白色角色截图已通过。
Windows 的双 CDC 角色识别已修正，相关 Windows/macOS 描述符测试共 23 项通过。

当前测试设备显示首次配置入口，需完成手机配置与配对，再验证真实对话、千问朗读、
角色安装和恢复。上述安装结果不代表全部功能验收完成，暂不发布稳定版。
