"""
Generated gRPC stubs (grpcio-tools output from proto/*.proto).

Regenerate with:
    python -m grpc_tools.protoc -I proto \
        --python_out=backend/generated --grpc_python_out=backend/generated \
        proto/control_signals.proto proto/ai_signaling.proto

protoc emits absolute imports (`import control_signals_pb2 ...`), so this
package prepends its own directory to sys.path to keep them importable.
"""
import os
import sys

_GENERATED_DIR = os.path.dirname(os.path.abspath(__file__))
if _GENERATED_DIR not in sys.path:
    sys.path.insert(0, _GENERATED_DIR)
