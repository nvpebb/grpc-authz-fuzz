# grpc-authz-fuzz

> **Status: WIP.** Not ready for production use.

Stateful, coverage-guided fuzzer for gRPC services aimed at access-control bugs
(BOLA, unary/streaming authorization divergence, interceptor bypass) via
differential oracles. Infers inter-RPC dependencies from protobuf schemas.
Instruments C++ servers with SanitizerCoverage. No LLMs.
