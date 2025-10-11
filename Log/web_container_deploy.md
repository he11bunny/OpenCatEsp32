# Petoi Web Block

## Container

### create image

```bash
docker build -t petoi-web-block .

# preview image
docker run -d -p 8080:80 --name petoi-web-block-container petoi-web-block
```

### publish image

```bash
docker tag petoi-web-block:latest petoi/petoi-web-block:latest
docker push petoi/petoi-web-block:latest
```

### deploy image

```bash
docker run -d -p 8080:80 --name petoi-serv petoi/petoi-web-block:latest
```