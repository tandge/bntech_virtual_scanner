\---

layout: page

title: 文章归档

permalink: /archive/

\---



{% assign posts\_by\_year = site.posts | group\_by\_exp: "post", "post.date | date: '%Y'" %}



{% for year in posts\_by\_year %}

\## {{ year.name }} 年



{% assign posts\_by\_month = year.items | group\_by\_exp: "post", "post.date | date: '%m'" %}



{% for month in posts\_by\_month %}

\### {{ month.name }} 月



<ul>

{% for post in month.items %}

&#x20; <li>

&#x20;   <a href="{{ post.url }}">{{ post.title }}</a>

&#x20;   <span style="color: #666; font-size: 0.9em;">({{ post.date | date: "%Y-%m-%d" }})</span>

&#x20;   {% if post.tags.size > 0 %}

&#x20;   <br>

&#x20;   <span style="font-size: 0.85em;">

&#x20;     {% for tag in post.tags %}

&#x20;     <span class="tag" style="background: #eef; padding: 2px 6px; border-radius: 3px; margin-right: 4px;">{{ tag }}</span>

&#x20;     {% endfor %}

&#x20;   </span>

&#x20;   {% endif %}

&#x20; </li>

{% endfor %}

</ul>

{% endfor %}

{% endfor %}



\---



\*\*总计\*\*: {{ site.posts.size }} 篇文章

