\---

layout: page

title: 文章分类

permalink: /categories/

\---



{% assign categories = site.categories | sort %}



\## 分类导航



<div style="margin-bottom: 30px;">

{% for category in categories %}

&#x20; <a href="#{{ category\[0] | slugify }}" style="display: inline-block; background: #0366d6; color: white; padding: 5px 12px; border-radius: 20px; margin-right: 8px; margin-bottom: 8px; text-decoration: none;">

&#x20;   {{ category\[0] }} <span style="background: white; color: #0366d6; padding: 1px 5px; border-radius: 10px; font-size: 0.8em;">{{ category\[1].size }}</span>

&#x20; </a>

{% endfor %}

</div>



\---



{% for category in categories %}

\## <a name="{{ category\[0] | slugify }}"></a>{{ category\[0] }}



<ul>

{% for post in category\[1] %}

&#x20; <li>

&#x20;   <a href="{{ post.url }}">{{ post.title }}</a>

&#x20;   <span style="color: #666; font-size: 0.9em;">({{ post.date | date: "%Y-%m-%d" }})</span>

&#x20; </li>

{% endfor %}

</ul>



\[↑ 返回顶部](#top)

\---

{% endfor %}

