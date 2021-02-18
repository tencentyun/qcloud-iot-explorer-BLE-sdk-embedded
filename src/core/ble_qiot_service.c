/*
 * Copyright (C) 2019 THL A29 Limited, a Tencent company. All rights reserved.
 * Licensed under the MIT License (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://opensource.org/licenses/MIT
 * Unless required by applicable law or agreed to in writing, software distributed under the License is
 * distributed on an "AS IS" basis, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
#ifdef __cplusplus
extern "C" {
#endif

#include "ble_qiot_service.h"

#include <stdio.h>

#include "ble_qiot_export.h"
#include "ble_qiot_import.h"
#include "ble_qiot_llsync_data.h"
#include "ble_qiot_llsync_device.h"
#include "ble_qiot_llsync_event.h"
#include "ble_qiot_log.h"
#include "ble_qiot_llsync_ota.h"
#include "ble_qiot_param_check.h"
#include "ble_qiot_service.h"
#include "ble_qiot_template.h"

// llsync support data fragment, so we need to package all the data before parsing if the data is slice
static ble_event_slice_t sg_ble_slice_data;

#if (1 == BLE_QIOT_BUTTON_BROADCAST)
static ble_timer_t sg_bind_timer = NULL;
#endif

static qiot_service_init_s service_info = {
    .service_uuid16  = IOT_BLE_UUID_SERVICE,
    .service_uuid128 = IOT_BLE_UUID_BASE,
    .device_info =
        {
            .uuid16          = IOT_BLE_UUID_DEVICE_INFO,
            .gatt_char_props = GATT_CHAR_WRITE,
            .on_write        = ble_device_info_write_cb,
        },
    .data =
        {
            .uuid16          = IOT_BLE_UUID_DATA,
            .gatt_char_props = GATT_CHAR_WRITE,
            .on_write        = ble_lldata_write_cb,
        },
    .event =
        {
            .uuid16          = IOT_BLE_UUID_EVENT,
            .gatt_char_props = GATT_CHAR_NOTIFY,
            .on_write        = NULL,
        },
    .ota =
        {
            .uuid16          = IOT_BLE_UUID_OTA,
            .gatt_char_props = GATT_CHAR_WRITE_WO_RESP,
            .on_write        = ble_ota_write_cb,
        },
};

const qiot_service_init_s *ble_get_qiot_services(void)
{
    return &service_info;
}

#if (1 == BLE_QIOT_BUTTON_BROADCAST)
static void ble_bind_timer_callback(void *param)
{
    ble_qiot_log_i("timer timeout");
    if (E_LLSYNC_BIND_WAIT == llsync_bind_state_get()) {
        ble_advertising_stop();
        llsync_bind_state_set(E_LLSYNC_BIND_IDLE);
        ble_qiot_log_i("stop advertising");
    }
}
#endif

ble_qiot_ret_status_t ble_qiot_advertising_start(void)
{
    adv_info_s my_adv_info;
    uint16_t   uuids[1];
    uint8_t    adv_data[32] = {0};
    uint8_t    adv_data_len = 0;

    uuids[0]                       = IOT_BLE_UUID_SERVICE;
    my_adv_info.uuid_info.uuid_num = 1;
    my_adv_info.uuid_info.uuids    = uuids;

    if (E_LLSYNC_BIND_IDLE == llsync_bind_state_get()) {
#if (1 == BLE_QIOT_BUTTON_BROADCAST)
        if (NULL == sg_bind_timer) {
            sg_bind_timer = ble_timer_create(BLE_TIMER_ONE_SHOT_TYPE, ble_bind_timer_callback);
            if (NULL == sg_bind_timer) {
                ble_qiot_log_e("create bind timer failed");
                return BLE_QIOT_RS_ERR;
            }
        }
#endif

        ble_advertising_stop();

        llsync_bind_state_set(E_LLSYNC_BIND_WAIT);
        adv_data_len = ble_get_my_broadcast_data((char *)adv_data, sizeof(adv_data));
        my_adv_info.manufacturer_info.company_identifier = TENCENT_COMPANY_IDENTIFIER;
        my_adv_info.manufacturer_info.adv_data           = adv_data;
        my_adv_info.manufacturer_info.adv_data_len       = adv_data_len;
        ble_advertising_start(&my_adv_info);
        ble_qiot_log_i("start wait advertising");

#if (1 == BLE_QIOT_BUTTON_BROADCAST)
        ble_timer_start(sg_bind_timer, BLE_QIOT_BIND_TIMEOUT);
#endif
    } else if (E_LLSYNC_BIND_WAIT == llsync_bind_state_get()) {
        ble_advertising_stop();
        adv_data_len = ble_get_my_broadcast_data((char *)adv_data, sizeof(adv_data));
        my_adv_info.manufacturer_info.company_identifier = TENCENT_COMPANY_IDENTIFIER;
        my_adv_info.manufacturer_info.adv_data           = adv_data;
        my_adv_info.manufacturer_info.adv_data_len       = adv_data_len;
        ble_advertising_start(&my_adv_info);
        ble_qiot_log_i("restart wait advertising");

#if (1 == BLE_QIOT_BUTTON_BROADCAST)
        ble_timer_stop(sg_bind_timer);
        ble_timer_start(sg_bind_timer, BLE_QIOT_BIND_TIMEOUT);
#endif
    } else if (E_LLSYNC_BIND_SUCC == llsync_bind_state_get()) {
        ble_advertising_stop();
        adv_data_len = ble_get_my_broadcast_data((char *)adv_data, sizeof(adv_data));
        my_adv_info.manufacturer_info.company_identifier = TENCENT_COMPANY_IDENTIFIER;
        my_adv_info.manufacturer_info.adv_data           = adv_data;
        my_adv_info.manufacturer_info.adv_data_len       = adv_data_len;
        ble_advertising_start(&my_adv_info);
        ble_qiot_log_i("start bind advertising");
    } else {
        // do nothing
    }

    return BLE_QIOT_RS_OK;
}

ble_qiot_ret_status_t ble_qiot_advertising_stop(void)
{
    return 0 == ble_advertising_stop() ? BLE_QIOT_RS_OK : BLE_QIOT_RS_ERR;
}

ble_qiot_ret_status_t ble_qiot_explorer_init(void)
{
    ble_qiot_ret_status_t      ret_code     = BLE_QIOT_RS_OK;
    const qiot_service_init_s *service_info = NULL;

    ble_qiot_set_log_level(BLE_QIOT_LOG_LEVEL_INFO);

    service_info = ble_get_qiot_services();
    ble_services_add(service_info);

    ret_code = ble_init_flash_data();
    if (ret_code != BLE_QIOT_RS_OK) {
        ble_qiot_log_e("flash init failed, ret code %d", ret_code);
        return ret_code;
    }

    return ret_code;
}

void ble_device_info_write_cb(const uint8_t *buf, uint16_t len)
{
    (void)ble_device_info_msg_handle((const char *)buf, len);
}

void ble_lldata_write_cb(const uint8_t *buf, uint16_t len)
{
    (void)ble_lldata_msg_handle((const char *)buf, len);
}

void ble_ota_write_cb(const uint8_t *buf, uint16_t len)
{
    (void)ble_ota_msg_handle((const char *)buf, len);
}

// when gap get ble connect event, use this function
void ble_gap_connect_cb(void)
{
    ble_connection_state_set(E_BLE_CONNECTED);
}

// when gap get ble disconnect event, use this function
void ble_gap_disconnect_cb(void)
{
    llsync_mtu_update(0);
    llsync_connection_state_set(E_LLSYNC_DISCONNECTED);
    ble_connection_state_set(E_BLE_DISCONNECTED);
    ble_ota_stop();
}

static uint8_t ble_msg_type_header_len(uint8_t type)
{
    if (type == BLE_QIOT_GET_STATUS_REPLY_DATA_TYPE) {
        return BLE_QIOT_GET_STATUS_REPLY_HEADER_LEN;
    } else {
        return BLE_QIOT_DATA_FIXED_HEADER_LEN;
    }
}

static uint8_t ble_package_slice_data(uint8_t data_type, uint8_t flag, uint8_t header_len, const char *in_buf,
                                      int in_len)
{
    if (!BLE_QIOT_IS_SLICE_HEADER(flag)) {
        if (!sg_ble_slice_data.have_data) {
            ble_qiot_log_e("slice no header");
            return -1;
        }
        if (data_type != sg_ble_slice_data.type) {
            ble_qiot_log_e("msg type: %d != %d", data_type, sg_ble_slice_data.type);
            return -1;
        }
        if (sg_ble_slice_data.buf_len + (in_len - header_len) > sizeof(sg_ble_slice_data.buf)) {
            ble_qiot_log_e("too long data: %d > %d", sg_ble_slice_data.buf_len + (in_len - header_len),
                           sizeof(sg_ble_slice_data.buf));
            return -1;
        }
    }

    if (BLE_QIOT_IS_SLICE_HEADER(flag)) {
        if (sg_ble_slice_data.have_data) {
            ble_qiot_log_i("new data coming, clean the package buffer");
            memset(&sg_ble_slice_data, 0, sizeof(sg_ble_slice_data));
        }
        sg_ble_slice_data.have_data = true;
        sg_ble_slice_data.type      = data_type;
        // reserved space for payload length field
        sg_ble_slice_data.buf_len += header_len;
        sg_ble_slice_data.buf[0] = in_buf[0];
        memcpy(sg_ble_slice_data.buf + sg_ble_slice_data.buf_len, in_buf + header_len, in_len - header_len);
        sg_ble_slice_data.buf_len += (in_len - header_len);

        return 1;
    } else if (BLE_QIOT_IS_SLICE_BODY(flag)) {
        memcpy(sg_ble_slice_data.buf + sg_ble_slice_data.buf_len, in_buf + header_len, in_len - header_len);
        sg_ble_slice_data.buf_len += (in_len - header_len);
        return 1;
    } else {
        memcpy(sg_ble_slice_data.buf + sg_ble_slice_data.buf_len, in_buf + header_len, in_len - header_len);
        sg_ble_slice_data.buf_len += (in_len - header_len);

        return 0;
    }
}

int ble_device_info_msg_handle(const char *in_buf, int in_len)
{
    POINTER_SANITY_CHECK(in_buf, BLE_QIOT_RS_ERR_PARA);
    uint8_t  ch;
    char     out_buf[80] = {0};
    char *   p_data      = NULL;
    int      p_data_len  = 0;
    int      ret_len     = 0;
    uint16_t tmp_len     = 0;
    uint8_t  header_len  = 0;
    int      ret         = BLE_QIOT_RS_OK;
    // This flag is use to avoid attacker jump "ble_conn_get_authcode()" step, then
    // send 'E_DEV_MSG_CONN_SUCC' msg, and device straightly set 'E_LLSYNC_CONNECTED' flag.
    // This behavior make signature check useless lead to risk.
    static bool conn_flag = false;

    p_data     = (char *)in_buf;
    p_data_len = in_len;

    // E_DEV_MSG_SYNC_TIME, E_DEV_MSG_CONN_VALID, E_DEV_MSG_BIND_SUCC, E_DEV_MSG_UNBIND this 4 type
    // of message has more than one bytes data, it may cut to several slices, here need to merge them
    // together, other type message only has 1 byte data, not need merge.
    if ((in_len > 3) && BLE_QIOT_IS_SLICE_PACKAGE(in_buf[1])) {
        // ble_qiot_log_hex(BLE_QIOT_LOG_LEVEL_INFO, "slice", p_data, p_data_len);
        header_len = ble_msg_type_header_len(in_buf[0]);
        ret        = ble_package_slice_data(in_buf[0], in_buf[1], header_len, in_buf, in_len);
        if (ret < 0) {
            return BLE_QIOT_RS_ERR;
        } else if (ret == 0) {
            tmp_len = HTONS(sg_ble_slice_data.buf_len - header_len);
            memcpy(&sg_ble_slice_data.buf[1], &tmp_len, sizeof(tmp_len));
            p_data     = sg_ble_slice_data.buf;
            p_data_len = sg_ble_slice_data.buf_len;
        } else if (ret > 0) {
            return BLE_QIOT_RS_OK;
        }
    }
    // ble_qiot_log_hex(BLE_QIOT_LOG_LEVEL_INFO, "tlv", p_data, p_data_len);

    ch = p_data[0];
    switch (ch) {
        case E_DEV_MSG_SYNC_TIME:
            ret_len = ble_bind_get_authcode(p_data + 3, p_data_len - 3, out_buf, sizeof(out_buf));
            if (ret_len <= 0) {
                ble_qiot_log_e("get bind authcode failed");
                ret = BLE_QIOT_RS_ERR;
                break;
            }
            ret = ble_event_notify((uint8_t)BLE_QIOT_EVENT_UP_BIND_SIGN_RET, NULL, 0, out_buf, ret_len);
            break;
        case E_DEV_MSG_CONN_VALID:
            ret_len = ble_conn_get_authcode(p_data + 3, p_data_len - 3, out_buf, sizeof(out_buf));
            if (ret_len <= 0) {
                ble_qiot_log_e("get connect authcode failed");
                ret = BLE_QIOT_RS_ERR;
                break;
            }
            ret       = ble_event_notify((uint8_t)BLE_QIOT_EVENT_UP_CONN_SIGN_RET, NULL, 0, out_buf, ret_len);
            conn_flag = true;
            break;
        case E_DEV_MSG_BIND_SUCC:
            if (BLE_QIOT_RS_OK != ble_bind_write_result(p_data + 3, p_data_len - 3)) {
                ble_qiot_log_e("write bind result failed");
                ret = BLE_QIOT_RS_ERR;
            }
            break;
        case E_DEV_MSG_BIND_FAIL:
            ble_qiot_log_i("get msg bind fail");
            break;
        case E_DEV_MSG_UNBIND:
            ret_len = ble_unbind_get_authcode(p_data + 3, p_data_len - 3, out_buf, sizeof(out_buf));
            if (ret_len <= 0) {
                ble_qiot_log_e("get unbind authcode failed");
                ret = BLE_QIOT_RS_ERR;
                break;
            }
            ret = ble_event_notify((uint8_t)BLE_QIOT_EVENT_UP_UNBIND_SIGN_RET, NULL, 0, out_buf, ret_len);
            break;
        case E_DEV_MSG_CONN_SUCC:
            if (!conn_flag) {
                break;
            }
            conn_flag = false;
            ble_qiot_log_i("get msg connect success");
            llsync_connection_state_set(E_LLSYNC_CONNECTED);
            ret = ble_event_report_device_info();
            break;
        case E_DEV_MSG_CONN_FAIL:
            ble_qiot_log_i("get msg connect fail");
            break;
        case E_DEV_MSG_UNBIND_SUCC:
            ble_qiot_log_i("get msg unbind success");
            if (BLE_QIOT_RS_OK != ble_unbind_write_result()) {
                ble_qiot_log_e("write unbind result failed");
                ret = BLE_QIOT_RS_ERR;
            }
            break;
        case E_DEV_MSG_UNBIND_FAIL:
            ble_qiot_log_i("get msg unbind fail");
            break;
        case E_DEV_MSG_SET_MTU_RESULT:
            ble_inform_mtu_result(p_data + 1, p_data_len - 1);
            break;
        default:
            break;
    }
    memset(&sg_ble_slice_data, 0, sizeof(sg_ble_slice_data));

    return ret;
}

// lldata message from remote
int ble_lldata_msg_handle(const char *in_buf, int in_len)
{
    POINTER_SANITY_CHECK(in_buf, BLE_QIOT_RS_ERR_PARA);

    uint8_t  data_type   = 0;
    uint8_t  data_effect = 0;
    uint8_t  id          = 0;
    uint8_t  slice_flag  = 0;
    uint8_t  header_len  = 0;
    uint8_t  slice_type  = 0;
    uint16_t tmp_len     = 0;
    char *   p_data      = NULL;
    int      p_data_len  = 0;
    int      ret         = 0;

    if (!llsync_is_connected()) {
        ble_qiot_log_e("operation negate, device not connected");
        return BLE_QIOT_RS_ERR;
    }

    p_data     = (char *)in_buf;
    p_data_len = in_len;

    data_type = BLE_QIOT_PARSE_MSG_HEAD_TYPE(in_buf[0]);
    if (data_type >= BLE_QIOT_DATA_TYPE_BUTT) {
        ble_qiot_log_e("invalid data type: %d", data_type);
        return BLE_QIOT_RS_ERR;
    }
    data_effect = BLE_QIOT_PARSE_MSG_HEAD_EFFECT(in_buf[0]);
    if (data_effect >= BLE_QIOT_EFFECT_BUTT) {
        ble_qiot_log_e("invalid data eff: ect");
        return BLE_QIOT_RS_ERR;
    }
    id = BLE_QIOT_PARSE_MSG_HEAD_ID(in_buf[0]);
    ble_qiot_log_d("data type: %d, effect: %d, id: %d", data_type, data_effect, id);

    // if data is action_reply, control or get_status_reply, the data maybe need package
    if ((data_type == BLE_QIOT_MSG_TYPE_ACTION) || (in_buf[0] == BLE_QIOT_CONTROL_DATA_TYPE) ||
        (in_buf[0] == BLE_QIOT_GET_STATUS_REPLY_DATA_TYPE)) {
        slice_flag = (in_buf[0] == BLE_QIOT_GET_STATUS_REPLY_DATA_TYPE) ? in_buf[2] : in_buf[1];
        slice_type = (in_buf[0] == BLE_QIOT_GET_STATUS_REPLY_DATA_TYPE) ? in_buf[0] : data_type;

        // ble_qiot_log_hex(BLE_QIOT_LOG_LEVEL_INFO, "slice", p_data, p_data_len);
        if (BLE_QIOT_IS_SLICE_PACKAGE(slice_flag)) {
            header_len = ble_msg_type_header_len(slice_type);
            ret        = ble_package_slice_data(slice_type, slice_flag, header_len, in_buf, in_len);
            if (ret < 0) {
                return BLE_QIOT_RS_ERR;
            } else if (ret == 0) {
                tmp_len = HTONS(sg_ble_slice_data.buf_len - header_len);
                if (BLE_QIOT_GET_STATUS_REPLY_DATA_TYPE == slice_type) {
                    sg_ble_slice_data.buf[1] = in_buf[1];
                    memcpy(&sg_ble_slice_data.buf[2], &tmp_len, sizeof(tmp_len));
                } else {
                    memcpy(&sg_ble_slice_data.buf[1], &tmp_len, sizeof(tmp_len));
                }
                p_data     = sg_ble_slice_data.buf;
                p_data_len = sg_ble_slice_data.buf_len;
            } else if (ret > 0) {
                return BLE_QIOT_RS_OK;
            }
        }
    }
    // ble_qiot_log_hex(BLE_QIOT_LOG_LEVEL_INFO, "tlv", p_data, p_data_len);

    switch (data_type) {
        case BLE_QIOT_MSG_TYPE_PROPERTY:
            if (BLE_QIOT_EFFECT_REQUEST == data_effect) {
                // default E_BLE_DATA_DOWN_TYPE_CONTROL
                ret = ble_lldata_property_request_handle(p_data + 3, p_data_len - 3);
            } else if (BLE_QIOT_EFFECT_REPLY == data_effect) {
                // id means BLE_QIOT_DATA_DOWN_GET_STATUS_REPLY or BLE_QIOT_DATA_DOWN_REPORT_REPLY
                ret = ble_lldata_property_reply_handle(id, p_data + 1, p_data_len - 1);
            } else {
                ret = BLE_QIOT_RS_ERR;
            }
            break;
        case BLE_QIOT_MSG_TYPE_EVENT:
            if (BLE_QIOT_EFFECT_REPLY == data_effect) {
                ret = ble_lldata_event_handle(id, p_data + 1, p_data_len - 1);
            } else {
                ble_qiot_log_e("invalid event data effect");
                ret = BLE_QIOT_RS_ERR;
            }
            break;
        case BLE_QIOT_MSG_TYPE_ACTION:
            if (BLE_QIOT_EFFECT_REQUEST == data_effect) {
                ret = ble_lldata_action_handle(id, p_data + 3, p_data_len - 3);
            } else {
                ble_qiot_log_e("invalid action data effect");
                ret = BLE_QIOT_RS_ERR;
            }
            break;
        default:
            break;
    }
    memset(&sg_ble_slice_data, 0, sizeof(sg_ble_slice_data));

    return ret;
}

#if (1 == BLE_QIOT_SUPPORT_OTA)
static uint8_t ble_ota_type_header_len(uint8_t type)
{
    return BLE_QIOT_GET_OTA_REQUEST_HEADER_LEN;
}

int ble_ota_msg_handle(const char *buf, uint16_t len)
{
    POINTER_SANITY_CHECK(buf, BLE_QIOT_RS_ERR_PARA);

    uint8_t  data_type  = 0;
    int      ret        = BLE_QIOT_RS_OK;
    uint8_t  header_len = 0;
    uint8_t  slice_flag = 0;
    char *   p_data     = NULL;
    int      p_data_len = 0;
    uint16_t tmp_len    = 0;

    if (!llsync_is_connected()) {
        ble_qiot_log_e("upgrade forbidden, device not connected");
        return BLE_QIOT_RS_ERR;
    }

    data_type  = (buf[0] == BLE_QIOT_OTA_MSG_REQUEST) ? BLE_QIOT_OTA_MSG_REQUEST : buf[0];
    slice_flag = (buf[0] == BLE_QIOT_OTA_MSG_REQUEST) ? buf[1] : buf[0];
    if (data_type >= BLE_QIOT_OTA_MSG_BUTT) {
        ble_qiot_log_e("invalid data type %d", data_type);
        return BLE_QIOT_RS_ERR;
    }
    p_data     = (char *)buf;
    p_data_len = len;

    // ble_qiot_log_i("ota data type %d, flag %d", data_type, slice_flag);
    if (BLE_QIOT_IS_SLICE_PACKAGE(slice_flag)) {
        ble_qiot_log_hex(BLE_QIOT_LOG_LEVEL_INFO, "tlv", p_data, p_data_len);
        header_len = ble_ota_type_header_len(data_type);
        ret        = ble_package_slice_data(data_type, slice_flag, header_len, buf, len);
        if (ret < 0) {
            return BLE_QIOT_RS_ERR;
        } else if (ret == 0) {
            if (data_type == BLE_QIOT_OTA_MSG_REQUEST) {
                tmp_len = HTONS(sg_ble_slice_data.buf_len - header_len);
                memcpy(&sg_ble_slice_data.buf[1], &tmp_len, sizeof(tmp_len));
            } else {
                sg_ble_slice_data.buf[1] = sg_ble_slice_data.buf_len - header_len;
            }
            if (data_type == BLE_QIOT_OTA_MSG_DATA) {
                sg_ble_slice_data.buf[2] = buf[2];
            }
            p_data     = sg_ble_slice_data.buf;
            p_data_len = sg_ble_slice_data.buf_len;
        } else if (ret > 0) {
            return BLE_QIOT_RS_OK;
        }
    }

    // ble_qiot_log_hex(BLE_QIOT_LOG_LEVEL_INFO, "tlv", p_data, p_data_len);
    switch (data_type) {
        case BLE_QIOT_OTA_MSG_REQUEST:
            ret = ble_ota_request_handle(p_data + 3, p_data_len);
            break;
        case BLE_QIOT_OTA_MSG_DATA:
            ret = ble_ota_data_handle(p_data + 2, p_data_len - 2);
            break;
        case BLE_QIOT_OTA_MSG_END:
            ret = ble_ota_file_end_handle();
            break;
        default:
            break;
    }
    memset(&sg_ble_slice_data, 0, sizeof(sg_ble_slice_data));

    return ret;
}
#else
int ble_ota_msg_handle(const char *buf, uint16_t len)
{
    return BLE_QIOT_RS_OK;
}
#endif

#ifdef __cplusplus
}
#endif
