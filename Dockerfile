FROM debian:12-slim

ARG BIN

RUN apt-get update && apt-get install -y libssl3 && rm -rf /var/lib/apt/lists/*

COPY release-artifacts/${BIN} /usr/local/bin/vigilant

RUN chmod +x /usr/local/bin/vigilant

CMD ["vigilant"]