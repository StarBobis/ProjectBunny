import DefaultTheme from 'vitepress/theme'
import { h } from 'vue'
import DocumentationBreadcrumb from './DocumentationBreadcrumb.vue'
import DocumentationHome from './DocumentationHome.vue'
import './style.css'

export default {
  extends: DefaultTheme,
  Layout: () =>
    h(DefaultTheme.Layout, null, {
      'home-hero-before': () => h(DocumentationHome),
      'doc-before': () => h(DocumentationBreadcrumb),
    }),
}
