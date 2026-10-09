# Docked preview (#245)

This fixture mirrors the report: a server configuration with long lines, edited
beside its rendered page. 编辑器和预览并排显示，预览固定在右侧，不再像一个窗口浮在编辑器上方。

## Server block

```nginx
server {
    listen 443 ssl http2;
    server_name example.com www.example.com static.example.com api.example.com;
    ssl_certificate /etc/letsencrypt/live/example.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/example.com/privkey.pem;
    add_header Strict-Transport-Security "max-age=63072000; includeSubDomains; preload" always;
    location / {
        proxy_pass http://127.0.0.1:8080;
        proxy_set_header Host $host;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    }
}
```

## Settings

| Directive | Value | 说明 |
| --- | --- | --- |
| `worker_processes` | `auto` | 按 CPU 核心数启动工作进程 |
| `keepalive_timeout` | `65` | Seconds an idle connection stays open |
| `client_max_body_size` | `20m` | Largest accepted upload |

## Checklist

- [x] The preview is docked beside the source
- [ ] Long lines scroll sideways in the editor while word wrap is off
- 快捷键 **Ctrl+Shift+E** 打开全宽阅读视图

> Hints fade after a moment and leave the bell alone.

Throughput scales as $T = \frac{n \cdot r}{1 + \alpha(n - 1)}$ for $n$ workers.

## Final heading

Last line of the fixture.
