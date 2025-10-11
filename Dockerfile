# 选择一个基础镜像，这里使用带有 Nginx 的 Alpine 版本
FROM nginx:alpine

# 将你的静态网站文件复制到 Nginx 的默认 Web 根目录
COPY ./PetoiWebBlock /usr/share/nginx/html
COPY ./PetoiWebBlock/programblockly.html /usr/share/nginx/html/index.html

# 暴露容器的 80 端口
EXPOSE 80

# 运行 Nginx 服务
CMD ["nginx", "-g", "daemon off;"]
