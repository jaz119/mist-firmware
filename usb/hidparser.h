#ifndef HIDPARSER_H
#define HIDPARSER_H

#include "attrs.h"

#define REPORT_TYPE_NONE     0
#define REPORT_TYPE_MOUSE    1
#define REPORT_TYPE_KEYBOARD 2
#define REPORT_TYPE_JOYSTICK 3

#define MAX_AXES             4
#define MAX_BUTTONS          16

typedef struct {
  uint16_t offset;
  uint8_t size;
  struct {
    int16_t min;
    uint16_t max;
  } logical;
  int32_t mul;
} hid_axis_t;

typedef struct {
    uint8_t byte_offset;
    uint8_t bitmask;
} hid_button_t;

// currently only joysticks are supported
typedef struct {
  uint8_t type: 2;             // REPORT_TYPE_...
  uint8_t report_id;
  uint8_t report_size;

  union {
    struct {
      hid_axis_t axis[MAX_AXES]; // x and y axis + wheel or right hat
      hid_button_t button[MAX_BUTTONS];
      uint8_t button_count;

      struct {
        uint16_t offset;
        uint8_t size;
        struct {
          int16_t min;
          uint16_t max;
        } logical;
        struct {
          int16_t min;
          uint16_t max;
        } physical;
      } hat;                    // 1 hat (joystick only)
    } joystick_mouse;
  };
} hid_report_t;

bool parse_report_descriptor(uint8_t *rep, uint16_t rep_size, hid_report_t *conf, int target_type);

#endif // HIDPARSER_H
