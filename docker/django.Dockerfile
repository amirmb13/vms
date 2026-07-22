# Control Plane image: Django REST API + orchestrator.
FROM python:3.12-slim

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1

# Build deps for psycopg / python-ldap (Active Directory support)
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential libpq-dev libldap2-dev libsasl2-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY backend/requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# Application code + protos, then regenerate gRPC stubs inside the image.
COPY backend/ .
COPY proto/ /proto/
RUN mkdir -p generated && \
    python -m grpc_tools.protoc -I /proto \
      --python_out=generated --grpc_python_out=generated \
      /proto/control_signals.proto /proto/ai_signaling.proto

EXPOSE 8000

CMD ["gunicorn", "config.wsgi:application", "--bind", "0.0.0.0:8000", "--workers", "4"]
