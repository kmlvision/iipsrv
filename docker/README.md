# IIPServer in Docker

Docker configuration for wrapping IIPImage in an nginx reverse proxy.

## Building and publishing docker image

Run 

```bash
# change to the root directory, set the <version> and build the image
cd iipsrv
# build the image with a tag
docker build -t kmlvision/iipsrv:<version> .

# publish the image into the docker registry
docker push kmlvision/iipsrv:<version> 
```



## Version 1.1.0
Features:
- CORS disabled
- health check API

## Version 1.0.0
Features:
- single upstream process (fastcgi)
- CORS enabled