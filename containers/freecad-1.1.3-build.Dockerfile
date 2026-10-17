FROM ubuntu:26.04@sha256:61ebaa5cc23ca45450db85eac015435199ec569e28ec222ea13f2aed2110b8a6

ARG UBUNTU_APT_SNAPSHOT=20261017T000000Z
ENV DEBIAN_FRONTEND=noninteractive

COPY containers/freecad-1.1.3-build.lock /tmp/freecad-build.lock

RUN set -eux; \
    apt-get -o APT::Update::Error-Mode=any update; \
    grep -E '^(ca-certificates|libssl3t64|openssl|openssl-provider-legacy)=' /tmp/freecad-build.lock \
        | xargs -r apt-get install --yes --no-install-recommends; \
    apt-get -o APT::Update::Error-Mode=any update --snapshot "${UBUNTU_APT_SNAPSHOT}"; \
    grep -Ev '^[[:space:]]*(#|$)' /tmp/freecad-build.lock \
        | xargs -r apt-get install --yes --no-install-recommends \
            --snapshot "${UBUNTU_APT_SNAPSHOT}"; \
    rm -rf /var/lib/apt/lists/* /tmp/freecad-build.lock

WORKDIR /workspace
CMD ["bash", "/workspace/scripts/reproducible-freecad-check.sh"]
