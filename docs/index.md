---
# YAML Front Matter - 页面元数据配置
layout: home          # 使用home布局（minima主题自带）
title: 我的开发日志    # 页面标题
permalink: /          # 页面永久链接
author: 你的名字       # 作者信息
last_modified_at: 2026-05-25  # 最后更新时间
---

# 欢迎来到我的开发日志 👋

这里记录了我在软件开发过程中的学习心得、技术探索和项目实践。

## 📌 关于我

- 🔭 目前专注于 **C++ 系统开发** 和 **Windows 驱动程序**
- 📚 正在学习 **TWAIN 协议** 和 **虚拟扫描仪技术**
- 💡 对 **低代码开发** 和 **AI 辅助编程** 有浓厚兴趣
- 📫 联系我: [your-email@example.com](mailto:your-email@example.com)
- 🌐 GitHub: [@your-username](https://github.com/your-username)

## 🚀 最新文章

{% for post in site.posts limit:5 %}
### [{{ post.title }}]({{ post.url }})
{{ post.excerpt | strip_html | truncate: 150 }}
**发布日期**: {{ post.date | date: "%Y-%m-%d" }}
{% if post.tags.size > 0 %}
**标签**: {% for tag in post.tags %}<span class="tag">{{ tag }}</span>{% unless forloop.last %}, {% endunless %}{% endfor %}
{% endif %}

---
{% endfor %}

[查看所有文章 →](/archive)

## 💻 我的项目

### [虚拟扫描仪驱动](https://github.com/your-username/virtual-scanner)
一个基于TWAIN协议的Windows虚拟扫描仪驱动程序，支持多种图像格式和传输模式。

- **技术栈**: C++, Win32 API, TWAIN 2.4
- **状态**: 开发中
- **最新更新**: 2026-05-20 - 完成Memory传输模式实现

### [TWAIN 协议中文文档](https://github.com/your-username/twain-docs-cn)
TWAIN协议的中文翻译和详细解释，帮助开发者快速理解和实现TWAIN接口。

- **技术栈**: Markdown, GitHub Pages
- **状态**: 持续更新中
- **最新更新**: 2026-05-15 - 完成DSM接口部分翻译

## 📊 开发统计

| 项目 | 语言 | 提交次数 | 最后更新 |
|------|------|----------|----------|
| virtual-scanner | C++ | 128 | 2026-05-20 |
| twain-docs-cn | Markdown | 45 | 2026-05-15 |
| dev-blog | Markdown | 32 | 2026-05-25 |

## 🛠️ 技术栈

**编程语言**:
- ![C++](https://img.shields.io/badge/C++-00599C?style=flat-square&logo=c%2B%2B&logoColor=white)
- ![Python](https://img.shields.io/badge/Python-3776AB?style=flat-square&logo=python&logoColor=white)
- ![JavaScript](https://img.shields.io/badge/JavaScript-F7DF1E?style=flat-square&logo=javascript&logoColor=black)

**开发工具**:
- ![Visual Studio](https://img.shields.io/badge/Visual_Studio-5C2D91?style=flat-square&logo=visual%20studio&logoColor=white)
- 
- ![GitHub](https://img.shields.io/badge/GitHub-181717?style=flat-square&logo=github&logoColor=white)

## 📝 最近更新

- [x] 2026-05-25: 完成开发博客首页搭建
- [x] 2026-05-20: 实现虚拟扫描仪Memory传输模式
- [x] 2026-05-15: 翻译TWAIN协议DSM接口部分
- [ ] 2026-05-30: 完成Native传输模式实现
- [ ] 2026-06-05: 添加文件传输模式支持

## 📬 订阅与交流

- **RSS订阅**: [/feed.xml](/feed.xml)
- **GitHub Issues**: [提交问题或建议](https://github.com/tandge/bntech_virtual_scanner/issues)
- **邮箱**: tandge@gmail.com

---

*本博客使用 [GitHub Pages](https://pages.github.com/) + [Jekyll](https://jekyllrb.com/) 构建*