<script setup lang="ts">
import { withBase } from 'vitepress'

const versions = [
  {
    name: 'DirectX 11',
    dll: 'd3d11.dll',
    path: '/dx11/',
    links: [
      ['安装 DLL', '/dx11/'],
      ['管理 Mod', '/dx11/mods.html'],
      ['INI 与命名空间', '/dx11/configuration.html'],
      ['定位资源与着色器', '/dx11/hunting.html'],
    ],
  },
  {
    name: 'DirectX 12',
    dll: 'd3d12.dll',
    path: '/dx12/',
    links: [
      ['安装 DLL', '/dx12/'],
      ['管理 Mod', '/dx12/mods.html'],
      ['INI 与命名空间', '/dx12/configuration.html'],
      ['定位资源与着色器', '/dx12/hunting.html'],
    ],
  },
]
</script>

<template>
  <div class="docs-home">
    <header class="docs-home__intro">
      <h1>Project Bunny <span>文档</span></h1>
      <p>安装和配置 Mod、定位资源，以及为 DirectX 游戏编写替换。</p>
    </header>

    <section class="docs-home__options" aria-labelledby="version-heading">
      <h2 id="version-heading">选择游戏使用的 DirectX 版本</h2>
      <div class="docs-home__grid">
        <article v-for="version in versions" :key="version.path" class="docs-home__card">
          <header class="docs-home__card-title">
            <h3><a :href="withBase(version.path)">{{ version.name }}</a></h3>
            <code>{{ version.dll }}</code>
          </header>
          <ul>
            <li v-for="[label, url] in version.links" :key="url">
              <a :href="withBase(url)">{{ label }} <span aria-hidden="true">›</span></a>
            </li>
          </ul>
        </article>
      </div>
    </section>
  </div>
</template>
