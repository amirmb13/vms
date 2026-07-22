# VMS AI Engine: zero-copy shared-memory inference (OpenVINO / ONNX Runtime).
# MUST run with `--ipc=host` (see docker/docker-compose.yml) so it can map
# the /dev/shm/vms_frame_* ring buffers created by the native C++ server.
FROM python:3.12-slim

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1

WORKDIR /app

COPY ai_engine/requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# Application code + protos, then generate gRPC stubs inside the image.
COPY ai_engine/ .
COPY proto/ /proto/
RUN mkdir -p generated && \
    python -m grpc_tools.protoc -I /proto \
      --python_out=generated --grpc_python_out=generated \
      /proto/ai_signaling.proto

EXPOSE 50061

CMD ["python", "main.py"]
