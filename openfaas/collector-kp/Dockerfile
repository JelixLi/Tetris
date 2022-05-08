FROM ubuntu:18.04
RUN apt-get update && apt-get install -y python3-pip && apt-get install -y python3
RUN pip3 install kubernetes -i https://mirrors.aliyun.com/pypi/simple/
RUN pip3 install prometheus-api-client -i https://mirrors.aliyun.com/pypi/simple/
ADD models /models
ADD collector.py /collector.py
WORKDIR /
CMD ["python3","collector.py"]