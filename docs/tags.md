\---

layout: page

title: 文章标签

permalink: /tags/

\---



{% assign tags = site.tags | sort %}



\## 标签云



<div style="margin-bottom: 30px; line-height: 2.5;">

{% for tag in tags %}

&#x20; {% assign tag\_size = tag\[1].size | times: 4 | plus: 12 %}

&#x20; <a href="#{{ tag\[0] | slugify }}" style="font-size: {{ tag\_size }}px; margin-right: 10px; text-decoration: none; color: #0366d6;">

&#x20;   {{ tag\[0] }}

&#x20; </a>

{% endfor %}

</div>



\---



{% for tag in tags %}

\## <a name="{{ tag\[0] | slugify }}"></a>{{ tag\[0] }}



<ul>

{% for post in tag\[1] %}

&#x20; <li>

&#x20;   <a href="{{ post.url }}">{{ post.title }}</a>

&#x20;   <span style="color: #666; font-size: 0.9em;">({{ post.date | date: "%Y-%m-%d" }})</span>

&#x20; </li>

{% endfor %}

</ul>



\[↑ 返回顶部](#top)

\---

{% endfor %}

