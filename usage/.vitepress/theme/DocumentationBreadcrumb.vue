<script setup lang="ts">
import { useRoute, withBase } from 'vitepress'
import { computed } from 'vue'

const route = useRoute()
const section = computed(() => {
  const path = route.path.slice(withBase('/').length)
  if (path.startsWith('dx11/')) return { name: 'DirectX 11', url: '/dx11/' }
  if (path.startsWith('dx12/')) return { name: 'DirectX 12', url: '/dx12/' }
})
</script>

<template>
  <nav v-if="section" class="docs-breadcrumb" aria-label="文档路径">
    <a :href="withBase('/')">文档</a>
    <span aria-hidden="true">/</span>
    <a
      v-if="route.path !== withBase(section.url)"
      :href="withBase(section.url)"
    >{{ section.name }}</a>
    <span v-else aria-current="page">{{ section.name }}</span>
  </nav>
</template>
