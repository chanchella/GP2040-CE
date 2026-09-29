#include "oag/firmware/controller_audio_device.h"

#include <array>
#include <cstdint>

#include "tusb.h"

namespace {
constexpr std::uint32_t kSampleRate = 48000;

bool gStreaming = false;
bool gMute[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1] {};
std::int16_t gVolume[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1] {};
std::uint32_t gSampleRate = kSampleRate;
std::uint8_t gClockValid = 1;

audio_control_range_4_n_t(1) gSampleRateRange {
    1,
    {
        {
            kSampleRate,
            kSampleRate,
            0,
        },
    },
};

} // namespace

namespace oag::firmware {

bool controllerAudioDeviceStreaming() {
    return gStreaming && tud_audio_mounted();
}

std::uint16_t controllerAudioDeviceWrite(
    const void* data,
    std::uint16_t length
) {
    if (!controllerAudioDeviceStreaming() || data == nullptr || length == 0) {
        return 0;
    }
    return tud_audio_write(data, length);
}

} // namespace oag::firmware

extern "C" bool tud_audio_set_itf_cb(
    std::uint8_t rhport,
    tusb_control_request_t const* request
) {
    (void)rhport;
    if (request == nullptr) return false;

    const std::uint8_t interfaceNumber = TU_U16_LOW(request->wIndex);
    const std::uint8_t alternateSetting = TU_U16_LOW(request->wValue);

    // PC receiver uses interfaces 0..9. Controller microphone AudioControl
    // is interface 10 and AudioStreaming is interface 11.
    if (interfaceNumber == 11) {
        gStreaming = alternateSetting != 0;
        if (!gStreaming) {
            tud_audio_clear_ep_in_ff();
        }
    }
    return true;
}

extern "C" bool tud_audio_set_itf_close_EP_cb(
    std::uint8_t rhport,
    tusb_control_request_t const* request
) {
    (void)rhport;
    (void)request;
    gStreaming = false;
    tud_audio_clear_ep_in_ff();
    return true;
}

extern "C" bool tud_audio_set_req_ep_cb(
    std::uint8_t rhport,
    tusb_control_request_t const* request,
    std::uint8_t* buffer
) {
    (void)rhport;
    (void)request;
    (void)buffer;
    return false;
}

extern "C" bool tud_audio_set_req_itf_cb(
    std::uint8_t rhport,
    tusb_control_request_t const* request,
    std::uint8_t* buffer
) {
    (void)rhport;
    (void)request;
    (void)buffer;
    return false;
}

extern "C" bool tud_audio_set_req_entity_cb(
    std::uint8_t rhport,
    tusb_control_request_t const* request,
    std::uint8_t* buffer
) {
    if (request == nullptr || buffer == nullptr) return false;

    const std::uint8_t channel = TU_U16_LOW(request->wValue);
    const std::uint8_t selector = TU_U16_HIGH(request->wValue);
    const std::uint8_t entity = TU_U16_HIGH(request->wIndex);

    if (entity != 2 || channel > CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX) {
        return false;
    }
    if (request->bRequest != AUDIO_CS_REQ_CUR) {
        return false;
    }

    if (selector == AUDIO_FU_CTRL_MUTE) {
        if (request->wLength != sizeof(audio_control_cur_1_t)) return false;
        gMute[channel] =
            reinterpret_cast<audio_control_cur_1_t*>(buffer)->bCur != 0;
        return true;
    }

    if (selector == AUDIO_FU_CTRL_VOLUME) {
        if (request->wLength != sizeof(audio_control_cur_2_t)) return false;
        gVolume[channel] =
            reinterpret_cast<audio_control_cur_2_t*>(buffer)->bCur;
        return true;
    }

    (void)rhport;
    return false;
}

extern "C" bool tud_audio_get_req_ep_cb(
    std::uint8_t rhport,
    tusb_control_request_t const* request
) {
    (void)rhport;
    (void)request;
    return false;
}

extern "C" bool tud_audio_get_req_itf_cb(
    std::uint8_t rhport,
    tusb_control_request_t const* request
) {
    (void)rhport;
    (void)request;
    return false;
}

extern "C" bool tud_audio_get_req_entity_cb(
    std::uint8_t rhport,
    tusb_control_request_t const* request
) {
    if (request == nullptr) return false;

    const std::uint8_t channel = TU_U16_LOW(request->wValue);
    const std::uint8_t selector = TU_U16_HIGH(request->wValue);
    const std::uint8_t entity = TU_U16_HIGH(request->wIndex);

    if (entity == 1 && selector == AUDIO_TE_CTRL_CONNECTOR) {
        audio_desc_channel_cluster_t cluster {};
        cluster.bNrChannels = 1;
        cluster.bmChannelConfig =
            static_cast<audio_channel_config_t>(0);
        cluster.iChannelNames = 0;
        return tud_audio_buffer_and_schedule_control_xfer(
            rhport,
            request,
            &cluster,
            sizeof(cluster)
        );
    }

    if (entity == 2 && channel <= CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX) {
        if (selector == AUDIO_FU_CTRL_MUTE) {
            const std::uint8_t value = gMute[channel] ? 1 : 0;
            return tud_audio_buffer_and_schedule_control_xfer(
                rhport,
                request,
                const_cast<std::uint8_t*>(&value),
                sizeof(value)
            );
        }

        if (selector == AUDIO_FU_CTRL_VOLUME) {
            if (request->bRequest == AUDIO_CS_REQ_CUR) {
                return tud_audio_buffer_and_schedule_control_xfer(
                    rhport,
                    request,
                    &gVolume[channel],
                    sizeof(gVolume[channel])
                );
            }
            if (request->bRequest == AUDIO_CS_REQ_RANGE) {
                audio_control_range_2_n_t(1) range {};
                range.wNumSubRanges = 1;
                range.subrange[0].bMin = -90 * 256;
                range.subrange[0].bMax = 0;
                range.subrange[0].bRes = 256;
                return tud_audio_buffer_and_schedule_control_xfer(
                    rhport,
                    request,
                    &range,
                    sizeof(range)
                );
            }
        }
    }

    if (entity == 4) {
        if (selector == AUDIO_CS_CTRL_SAM_FREQ) {
            if (request->bRequest == AUDIO_CS_REQ_CUR) {
                return tud_audio_buffer_and_schedule_control_xfer(
                    rhport,
                    request,
                    &gSampleRate,
                    sizeof(gSampleRate)
                );
            }
            if (request->bRequest == AUDIO_CS_REQ_RANGE) {
                return tud_audio_buffer_and_schedule_control_xfer(
                    rhport,
                    request,
                    &gSampleRateRange,
                    sizeof(gSampleRateRange)
                );
            }
        }

        if (
            selector == AUDIO_CS_CTRL_CLK_VALID &&
            request->bRequest == AUDIO_CS_REQ_CUR
        ) {
            return tud_audio_buffer_and_schedule_control_xfer(
                rhport,
                request,
                &gClockValid,
                sizeof(gClockValid)
            );
        }
    }

    return false;
}
