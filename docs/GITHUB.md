# 上传到 GitHub

本目录是可独立构建的 PSP 源码仓库，不需要上传原工程的 tmp、dist、.claude 或游戏文件。

1. 在 GitHub 创建空仓库，例如 Native32PSP；可先不勾选自动生成 README 和 LICENSE。
2. 在本目录打开终端，执行下列命令，将 `<你的仓库地址>` 替换为真实地址：

```sh
git init -b main
git add .
git status --short
git commit -m "Import Native32PSP source and documentation"
git remote add origin <你的仓库地址>
git push -u origin main
```

提交前确认暂存内容没有游戏、个人配置、日志或密钥。所有合成媒体测试文件可随源码提交。

3. 在 GitHub 创建 Release，建议版本标签 `v1.3.0-psp.1`；使用 `CHANGELOG.md` 描述，上传整理好的 PSP 运行 ZIP。源码放 Git 仓库，EBOOT 运行包放 Release。

自行构建后可生成运行包：

```sh
python tools/package_release.py --eboot EBOOT.PBP --output dist/Native32PSP-PSP.zip
```

本次整理没有创建远程仓库、推送代码或公开发布。仓库地址与 Release 标签请按实际情况填写。
