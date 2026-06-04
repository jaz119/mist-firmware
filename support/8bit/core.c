#include <string.h>
#include <8bit/core.h>
#include <user_io.h>
#include <user_io_hid.h>
#include <spi.h>
#include <osd.h>
#include <arc_file.h>
#include <settings.h>
#include <data_io.h>
#include <pcecd.h>
#include <neocd.h>
#include <debug.h>

extern char s[OSD_BUF_SIZE];

// core variant (mostly for arcades)
int64_t core_mod = 0;

// extra features in the firmware requested by the core
uint32_t core_features = 0;

// keep state over core type and its capabilities
static char core_type_8bit_with_config_string = 0;

// max 16 bytes for core name
char core_name[16 + 1];

int conf_items = 0;
uint16_t conf_idx[CONF_TBL_MAX];

void user_io_send_core_mod()
{
    infof("Sending core mod = 0x" PRIu64_llx,
        PRIu64_LOW(core_mod), PRIu64_HIGH(core_mod));

    spi_uio_cmd8(UIO_SET_MOD, core_mod & 0x7f);
    spi_uio_cmd64(UIO_SET_MOD2, core_mod);
}

void user_io_set_core_mod(int64_t mod)
{
    core_mod = mod;
}

static void user_io_read_core_name()
{
    core_name[0] = 0;

    if (user_io_is_8bit_with_config_string())
    {
        const char *p = user_io_8bit_get_string(0); // get core name

        if (p && p[0])
            strncpy(core_name, p, sizeof(core_name));

        core_name[sizeof(core_name) - 1] = 0;
    }

    debugf("Core name from FPGA: %s", core_name);
}

const char *user_io_get_core_name()
{
    const char *arc_core_name = arc_get_corename();
    return *arc_core_name ? arc_core_name : core_name;
}

char user_io_create_config_name(char *s, const char *ext, uint8_t flags)
{
    const char *p = 0;

    if (flags & CONFIG_VHD)
        p = arc_get_vhdname();

    if (!p || !*p)
        p = user_io_get_core_name();

    if (p[0])
    {
        if (flags & CONFIG_ROOT)
            strcpy(s,"/");
        else
            s[0] = 0;

        strcat(s, p);
        if (ext)
        {
            strcat(s,".");
            strcat(s,ext);
        }

        return 0;
    }

    return 1;
}

char user_io_is_8bit_with_config_string()
{
    return core_type_8bit_with_config_string;
}

static void user_io_read_core_features()
{
    core_features = 0;

    spi_uio_cmd_cont(UIO_GET_FEATS);
    if (spi_in() == 0x80)
    {
        core_features = spi_in();
        core_features = (core_features << 8) | spi_in();
        core_features = (core_features << 8) | spi_in();
        core_features = (core_features << 8) | spi_in();
    }
    DisableIO();

    if (core_features & FEAT_PS2REP)
    {
        ps2_typematic_rate = 0x08;
    }
}

uint32_t user_io_get_core_features()
{
    return core_features;
}

// 8bit cores have a config string telling the firmware how to treat it
char *user_io_8bit_get_string(uint8_t index)
{
    unsigned char i, lidx = 0, d = 0, arc = 0;
    int arc_ptr = 0, j = 0;
    char dip[3];
    static char buffer[128 + 1]; // max 128 bytes per config item
    uint16_t start_chr;

    // clear buffer
    buffer[0] = 0;

    // use the config index table to get where to start
    // conf_idx stores the starting position of every 4th item
    // if the index is in a DIP setting, it has 0
    uint32_t pos = 0, lastpos = 0;

    i = index >> 2;
    while (i > 0 && (i > conf_items || conf_idx[i] == 0))
        i--;

    pos = lastpos = conf_idx[i];
    lidx = i << 2;

    // iprintf("index=%d cached pos=%d lidx=%d\n", index, pos, lidx);

    spi_uio_cmd_cont(UIO_GET_STR_EXT);
    i = SPI(pos & 0xff);

    if (i == 0xaa)
    {
        SPI(pos >> 8);
        i = spi_in(); // dummy byte to prepare to apply the offset in the core
        i = spi_in();
    }
    else
    {
        DisableIO();
        lidx = 0;
        pos = lastpos = 0;
        // iprintf("UIO_GET_STRING_EXT not supported\n");

        spi_uio_cmd_cont(UIO_GET_STRING);
        i = spi_in();

        // the first char returned will be 0xff if the core doesn't support
        // config strings. atari 800 returns 0xa4 which is the status byte
        if ((i == 0xff) || (i == 0xa4))
        {
            DisableIO();
            return NULL;
        }
    }

    while ((i != 0) && (i != 0xff) && (j < sizeof(buffer)))
    {
        if (i == ';')
        {
            if ((lidx & 0x03) == 0 && (lidx >> 2) < CONF_TBL_MAX)
            {
                conf_idx[lidx >> 2] = arc ? 0 : lastpos;
                if (conf_items < (lidx >> 2))
                    conf_items = (lidx >> 2);
            }

            lastpos = pos + 1;
            if (!arc && d == 3 && !strncmp(dip, "DIP", 3))
            {
                // found "DIP", continue with config snippet from ARC
                if (lidx == index) {
                    // skip the DIP line
                    j = 0;
                    buffer[0] = 0;
                }
                arc = 1;
            }
            else
            {
                if (lidx == index)
                {
                    buffer[j++] = 0;
                    break;
                }
                lidx++;
            }
            d = 0;
        }
        else
        {
            if(lidx == index)
                buffer[j++] = i;
            if (d < 3)
                dip[d++] = i;
        }

        if (arc)
        {
            i = arc_get_conf()[arc_ptr++];
            if (!i) arc = 0;
        }

        if (!arc)
        {
            i = spi_in();
            pos++;
        }
    }

    DisableIO();

    // if this was the last string in the config string list,
    // then it still needs to be terminated
    if (lidx == index)
        buffer[j] = 0;

    // also return NULL for empty strings
    if (!buffer[0])
        return NULL;

    return buffer;
}

uint64_t user_io_8bit_set_status(uint64_t new_status, uint64_t mask)
{
    static uint64_t status = 0;

    // if mask is 0 just return the current status
    if (mask)
    {
        // keep everything not masked
        status &= ~mask;

        // updated masked bits
        status |= new_status & mask;

        spi_uio_cmd8(UIO_SET_STATUS, status);
        spi_uio_cmd64(UIO_SET_STATUS2, status);
    }

    return status;
}

uint8_t user_io_ext_idx(const char *name, const char *ext)
{
    unsigned int idx = 0;
    char ext3[4]; // extension truncated or extended to 3 chars
    int len = strlen(ext);
    int extlen;

    const char *nameext = GetExtension(name);
    if (!nameext)
        return 0;

    extlen = strlen(nameext);

    for (int i = 0; i < 3; i++)
    {
        ext3[i] = i < extlen ? nameext[i] : ' ';
    }

    ext3[3] = 0;

    while ((len > 3) && *ext)
    {
        if (!_strnicmp(ext3, ext, 3))
            return idx;

        if (strlen(ext) <= 3)
            break;

        idx++;
        ext +=3;
    }

    return 0;
}

static void generic_8bit_init()
{
    UINT br;
    FIL file;

    // send core variant first to allow the FPGA choosing the config string
    user_io_send_core_mod();

    // forward SD card config to core in case
    // it uses the local SD card implementation
    user_io_sd_set_config();

    // check if core has a config string
    core_type_8bit_with_config_string = (user_io_8bit_get_string(0) != NULL);

    // set core name, this currently only sets a name for the 8bit cores
    user_io_read_core_name();

    // get requested features
    user_io_read_core_features();

    // send a reset
    user_io_8bit_set_status(UIO_STATUS_RESET, ~0);

    // try to load config
    if (!user_io_create_config_name(s, "CFG", CONFIG_ROOT))
    {
        debugf("Loading config %s", s);

        if (f_open(&file, s, FA_READ) == FR_OK)
        {
            debugf("Found config");

            if (f_size(&file) <= 8)
            {
                ((unsigned long long*)sector_buffer)[0] = 0;
                f_read(&file, sector_buffer, f_size(&file), &br);
                user_io_8bit_set_status(((unsigned long long*)sector_buffer)[0], ~1);
            } else {
                settings_load(false);
            }

            f_close(&file);
        }
        else
        {
            user_io_8bit_set_status(arc_get_default(), ~1);
        }
    }

    // check if there's a <core>.rom or <core>.r0[1-6] present, send it via index 0-6
    for (int i = 0; i < 7; i++)
    {
        char ext[4];

        if (!i) {
            strcpy(ext, "ROM");
        } else {
            strcpy(ext, "R01");
            ext[2] = '0'+i;
        }

        for (char root = 0; root <= 1; root++)
        {
            if (!user_io_create_config_name(s, ext, root))
            {
                debugf("Looking for %s", s);

                if (f_open(&file, s, FA_READ) == FR_OK)
                {
                    data_io_file_tx(&file, i, ext);
                    f_close(&file);
                    break;
                }
            }
        }
    }

    if (!user_io_create_config_name(s, "RAM", CONFIG_ROOT))
    {
        debugf("Looking for %s", s);

        // check if there's a <core>.ram present, send it via index -1
        if (f_open(&file, s, FA_READ) == FR_OK)
        {
            data_io_file_tx(&file, -1, "RAM");
            f_close(&file);
        }
    }

    for (int i = 0; i < ARRAY_SIZE(sd_image); i++)
    {
        hardfile[i] = &hardfiles[i];

        if ((core_features & (FEAT_IDE0 << (2 * i))) == (FEAT_IDE0_CDROM << (2 * i)))
        {
            debugf("IDE %d: ATAPI CDROM", i);
            hardfiles[i].enabled = HDF_CDROM;
            OpenHardfile(i, false);
        }
    }

    // check if there's a <core>.vhd present
    if (!user_io_create_config_name(s, "VHD", CONFIG_ROOT | CONFIG_VHD))
    {
        debugf("Looking for %s", s);

        if (!(core_features & FEAT_IDE0))
        {
            user_io_file_mount(s, 0);
        }

        if (!user_io_is_mounted(0))
        {
            // check for <core>.HD0/1 files
            if (!user_io_create_config_name(s, "HD0", CONFIG_ROOT | CONFIG_VHD))
            {
                for (int i = 0; i < ARRAY_SIZE(sd_image); i++)
                {
                    s[strlen(s)-1] = '0' + i;
                    debugf("Looking for %s", s);

                    if ((core_features & (FEAT_IDE0 << (2 * i))) == (FEAT_IDE0_ATA << (2 * i)))
                    {
                        debugf("IDE %d: ATA Hard Disk", i);
                        hardfiles[i].enabled = HDF_FILE;
                        sniprintf(hardfiles[i].path, sizeof(hardfiles[0].path), "%s", s);
                        OpenHardfile(i, false);
                    } else {
                        user_io_file_mount(s, i);
                    }
                }
            }
        }
    }

    if (core_features & FEAT_IDE_MASK)
    {
        SendHDFCfg();
    }

    // release reset
    user_io_8bit_set_status(0, UIO_STATUS_RESET);
}

static void generic_8bit_poll()
{
    if ((core_features & FEAT_PCECD) || !strcmp(user_io_get_core_name(), "TGFX16"))
    {
        pcecd_poll();
    }
    else if (core_features & FEAT_NEOCD)
    {
        neocd_poll();
    }

    handle_mouse_events_ps2();
    handle_mouse_commands_ps2();

    handle_typematic_repeat_ps2();
    handle_kbd_commands_ps2();
}

static void generic_8bit_reset(bool)
{
    kbd_reset = 1;
}

// core iface
const user_io_core_t generic_core = {
    .init = generic_8bit_init,
    .poll = generic_8bit_poll,
    .reset = generic_8bit_reset,
    .keycode = keycode_ps2,
    .modify_keycode = modify_keycode_ps2,
    .send_keycode = send_keycode_ps2,
    .send_mouse = send_mouse_ps2,
    .send_analog_joy = send_analog_joystick,
    .send_digital_joy = send_digital_joystick,
    // .eject_all = TODO,
    .name = "8BIT",
};
