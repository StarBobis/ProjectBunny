import { defineConfig } from 'vitepress'

export default defineConfig({
  base: process.env.VITEPRESS_BASE || '/',
  lang: 'zh-CN',
  title: 'Project Bunny',
  description: 'Project Bunny DLL 使用文档',
  srcExclude: ['README.md'],
  themeConfig: {
    nav: [
      { text: 'DX11', link: '/dx11/', activeMatch: '^/dx11/' },
      { text: 'DX12', link: '/dx12/', activeMatch: '^/dx12/' }
    ],
    sidebar: {
      '/dx11/': [{ text: '使用', items: [
        { text: '安装', link: '/dx11/' },
        { text: '管理 Mod', link: '/dx11/mods' },
        { text: '按键与变量', link: '/dx11/keys' },
        { text: '定位与导出', link: '/dx11/hunting' },
        { text: '编写替换', link: '/dx11/overrides' },
        { text: '常用写法', link: '/dx11/recipes' },
        { text: '排错', link: '/dx11/troubleshooting' }
      ] }, { text: 'INI 语言', collapsed: false, items: [
        { text: 'INI 与命名空间', link: '/dx11/configuration' },
        { text: '变量与持久设置', link: '/dx11/variables' },
        { text: '表达式', link: '/dx11/expressions' },
        { text: '运行时参数', link: '/dx11/runtime-values' },
        { text: '命令列表与执行时机', link: '/dx11/command-lists' },
        { text: '预设、过渡与输入开关', link: '/dx11/presets-input' }
      ] }, { text: '资源', collapsed: false, items: [
        { text: '声明资源', link: '/dx11/resources' },
        { text: '槽位、复制与引用', link: '/dx11/resource-copy' },
        { text: '区域与范围', link: '/dx11/regions' },
        { text: '资源元数据', link: '/dx11/metadata' },
        { text: '资源池与变量池', link: '/dx11/pools' },
        { text: 'GPU 读回', link: '/dx11/readback' },
        { text: '感知纹理匹配', link: '/dx11/perceptual-hash' }
      ] }, { text: '绘制与着色器', collapsed: false, items: [
        { text: 'TextureOverride', link: '/dx11/texture-overrides' },
        { text: 'ShaderOverride 与文件补丁', link: '/dx11/shaders' },
        { text: 'ShaderRegex', link: '/dx11/shader-regex' },
        { text: 'CustomShader', link: '/dx11/custom-shaders' },
        { text: '绘制、Dispatch 与清理', link: '/dx11/draw' },
        { text: '输入布局与流输出', link: '/dx11/layouts' }
      ] }, { text: '工具与配置', collapsed: false, items: [
        { text: '帧分析', link: '/dx11/frame-analysis' },
        { text: '全局设置', link: '/dx11/settings' }
      ] }],
      '/dx12/': [{ text: '使用', items: [
        { text: '安装', link: '/dx12/' },
        { text: '管理 Mod', link: '/dx12/mods' },
        { text: '按键与变量', link: '/dx12/keys' },
        { text: '定位与导出', link: '/dx12/hunting' },
        { text: '编写替换', link: '/dx12/overrides' },
        { text: '常用写法', link: '/dx12/recipes' },
        { text: '排错', link: '/dx12/troubleshooting' }
      ] }, { text: 'INI 语言', collapsed: false, items: [
        { text: 'INI 与命名空间', link: '/dx12/configuration' },
        { text: '变量与表达式', link: '/dx12/expressions' },
        { text: '运行时参数', link: '/dx12/runtime-values' },
        { text: '命令列表', link: '/dx12/command-lists' }
      ] }, { text: '资源与绘制', collapsed: false, items: [
        { text: '资源与引用', link: '/dx12/resources' },
        { text: 'TextureOverride 与模型替换', link: '/dx12/texture-overrides' },
        { text: '着色器覆盖与模型筛选', link: '/dx12/shaders' },
        { text: 'CustomShader', link: '/dx12/custom-shaders' }
      ] }, { text: '工具与兼容', collapsed: false, items: [
        { text: '帧分析与性能诊断', link: '/dx12/frame-analysis' },
        { text: 'DX11 配置迁移范围', link: '/dx12/compatibility' }
      ] }]
    },
    socialLinks: [
      { icon: 'github', link: 'https://github.com/StarBobis/ProjectBunny' }
    ],
    search: { provider: 'local' },
    outline: { label: '本页目录' },
    docFooter: { prev: '上一页', next: '下一页' },
    returnToTopLabel: '返回顶部',
    sidebarMenuLabel: '菜单',
    darkModeSwitchLabel: '外观'
  }
})
