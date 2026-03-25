FROM python:3.12-slim

ARG BIN

WORKDIR /app
COPY --chmod=755 release-artifacts/${BIN} /usr/local/bin/vigilant

EXPOSE 9000 9001
VOLUME ["/etc/vigilant/services"]

ENTRYPOINT ["/usr/local/bin/vigilant"]
CMD ["server", "-d", "/etc/vigilant/services", "-p", "9000", "-dash", "9001"]
