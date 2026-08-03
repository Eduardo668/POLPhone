# syntax=docker/dockerfile:1

# Debian 12.12 slim, official image, pinned to the linux/amd64 manifest digest.
FROM debian:12.12-slim@sha256:a1363ada3b45cb3ebc74c78943558f8b0c2b59aaa194d8224e1b02cfd5d78583 AS builder

ARG ASTERISK_TAG=20.19.0
ARG ASTERISK_COMMIT=f66917fe6da487155f80ab50403bf3d7bdc86183
ARG BUILD_JOBS=2

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        bison \
        build-essential \
        ca-certificates \
        curl \
        flex \
        git \
        libedit-dev \
        libjansson-dev \
        libncurses-dev \
        libsqlite3-dev \
        libssl-dev \
        libxml2-dev \
        pkg-config \
        uuid-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /usr/src
RUN git clone --depth 1 --branch "${ASTERISK_TAG}" https://github.com/asterisk/asterisk.git asterisk \
    && test "$(git -C asterisk rev-parse HEAD)" = "${ASTERISK_COMMIT}"

WORKDIR /usr/src/asterisk
RUN ./configure \
        --prefix=/usr \
        --sysconfdir=/etc \
        --localstatedir=/var \
        --libdir=/usr/lib \
        --with-pjproject-bundled \
        --without-dahdi \
        --without-pri \
        --without-tonezone \
        --without-x11 \
        --without-bluetooth \
        --without-postgres \
        --without-mysqlclient \
        --without-lua \
        --without-speex \
        --without-speexdsp \
        --without-srtp \
        --without-imap \
        --without-radius \
        --without-spandsp \
        --without-ical \
        --without-gtk2 \
        --without-sdl \
        --without-vorbis \
        --without-ogg \
    && make menuselect.makeopts \
    && menuselect/menuselect \
        --enable chan_sip \
        --enable chan_pjsip \
        --enable app_dial \
        --enable app_echo \
        --enable app_playback \
        --enable app_playtones \
        --enable app_read \
        --enable codec_alaw \
        --enable codec_ulaw \
        --enable format_pcm \
        --enable res_rtp_asterisk \
        --enable CORE-SOUNDS-EN-ULAW \
        --enable CORE-SOUNDS-EN-ALAW \
        --disable app_confbridge \
        --disable app_mixmonitor \
        --disable app_queue \
        --disable app_voicemail \
        --disable chan_dahdi \
        --disable chan_iax2 \
        --disable res_musiconhold \
        menuselect.makeopts \
    && make -j"${BUILD_JOBS}" \
    && make install DESTDIR=/opt/asterisk-root \
    && install -D -m 0644 COPYING /opt/asterisk-root/usr/share/licenses/asterisk/COPYING \
    && test -f /opt/asterisk-root/usr/lib/asterisk/modules/chan_sip.so \
    && test -f /opt/asterisk-root/usr/lib/asterisk/modules/chan_pjsip.so \
    && test -f /opt/asterisk-root/usr/lib/asterisk/modules/app_read.so

FROM debian:12.12-slim@sha256:a1363ada3b45cb3ebc74c78943558f8b0c2b59aaa194d8224e1b02cfd5d78583 AS runtime

ARG ASTERISK_TAG=20.19.0
ARG ASTERISK_COMMIT=f66917fe6da487155f80ab50403bf3d7bdc86183

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        ca-certificates \
        libedit2 \
        libjansson4 \
        libncurses6 \
        libsqlite3-0 \
        libssl3 \
        libtinfo6 \
        libuuid1 \
        libxml2 \
        socat \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --gid 10001 asterisk \
    && useradd --uid 10001 --gid 10001 --home-dir /var/lib/asterisk --no-create-home --shell /usr/sbin/nologin asterisk

COPY --from=builder /opt/asterisk-root/ /
COPY scripts/entrypoint.sh scripts/gateway.sh scripts/generate-config.sh scripts/healthcheck.sh /usr/local/bin/

RUN chmod 0555 /usr/local/bin/entrypoint.sh /usr/local/bin/gateway.sh /usr/local/bin/generate-config.sh /usr/local/bin/healthcheck.sh \
    && rm -rf /var/lib/asterisk/moh /var/lib/asterisk/static-http \
    && find /var/lib/asterisk/sounds -type f ! -name '*.ulaw' ! -name '*.alaw' -delete \
    && rm -f \
        /usr/lib/asterisk/modules/app_confbridge.so \
        /usr/lib/asterisk/modules/app_dictate.so \
        /usr/lib/asterisk/modules/app_mixmonitor.so \
        /usr/lib/asterisk/modules/app_queue.so \
        /usr/lib/asterisk/modules/app_record.so \
        /usr/lib/asterisk/modules/app_voicemail.so \
        /usr/lib/asterisk/modules/res_ari*.so \
        /usr/lib/asterisk/modules/res_http_websocket.so \
        /usr/lib/asterisk/modules/res_stasis_recording.so \
    && mkdir -p /etc/asterisk /run/asterisk /var/cache/asterisk /var/lib/asterisk /var/log/asterisk /var/spool/asterisk \
    && chown -R asterisk:asterisk /etc/asterisk /run/asterisk /var/cache/asterisk /var/lib/asterisk /var/log/asterisk /var/spool/asterisk \
    && test -x /usr/sbin/asterisk

LABEL org.opencontainers.image.title="POLPhone isolated Asterisk lab" \
      org.opencontainers.image.version="${ASTERISK_TAG}" \
      org.opencontainers.image.revision="${ASTERISK_COMMIT}" \
      org.opencontainers.image.source="https://github.com/asterisk/asterisk" \
      org.opencontainers.image.licenses="GPL-2.0-only" \
      org.opencontainers.image.description="Local-only SIP/DTMF test lab; not for production"

USER asterisk:asterisk
EXPOSE 5060/udp 5061/udp 10000-10100/udp

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
HEALTHCHECK --interval=5s --timeout=4s --start-period=20s --retries=12 CMD ["/usr/local/bin/healthcheck.sh"]
