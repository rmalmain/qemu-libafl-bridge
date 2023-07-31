#include "qemu/osdep.h"
#include "device-save.h"
#include "migration/qemu-file.h"
#include "io/channel-buffer.h"
#include "channel-buffer-writeback.h"
#include "migration/vmstate.h"
#include "qemu/main-loop.h"
#include "../syx-misc.h"

#include "migration/savevm.h"

int libafl_restoring_devices;

extern SaveState savevm_state;

extern void save_section_header(QEMUFile *f, SaveStateEntry *se, uint8_t section_type);
extern int vmstate_save(QEMUFile *f, SaveStateEntry *se, JSONWriter *vmdesc);
extern void save_section_footer(QEMUFile *f, SaveStateEntry *se);

// iothread must be locked
device_save_state_t* device_save_all(void) {
    return device_save_kind(DEVICE_SNAPSHOT_ALL, NULL);
}

static int is_in_list(char* str, char** list) {
    while (*list) {
        if (!strcmp(str, *list)) {
            return 1;
        }
        list++;
    }
    return 0;
}

device_save_state_t* device_save_kind(device_snapshot_kind_t kind, char** names) {
    device_save_state_t* dss = g_new0(device_save_state_t, 1);
    SaveStateEntry *se;

    dss->kind = DEVICE_SAVE_KIND_FULL;
    dss->save_buffer = g_new(uint8_t, QEMU_FILE_RAM_LIMIT);

    QIOChannelBufferWriteback* wbioc = qio_channel_buffer_writeback_new(QEMU_FILE_RAM_LIMIT, dss->save_buffer, QEMU_FILE_RAM_LIMIT, &dss->save_buffer_size);
    QIOChannel* ioc = QIO_CHANNEL(wbioc);

    QEMUFile* f = qemu_file_new_output(ioc);
    
    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        int ret;

        if (se->is_ram) {
            continue;
        }
        if (!strcmp(se->idstr, "globalstate")) {
            continue;
        }
        switch (kind) {
            case DEVICE_SNAPSHOT_ALLOWLIST:
                if (!is_in_list(se->idstr, names)) {
                    continue;
                }
                break;
            case DEVICE_SNAPSHOT_DENYLIST:
                if (is_in_list(se->idstr, names)) {
                    continue;
                }
                break;
            default:
                break;
        }

        // SYX_PRINTF("Saving section %s...\n", se->idstr);

        ret = vmstate_save(f, se, NULL);

        if (ret) {
            SYX_PRINTF("Device save all error: %d\n", ret);
            abort();
        }
    }

    printf("\n");

    qemu_put_byte(f, QEMU_VM_EOF);

    qemu_fclose(f);

    return dss;
}

void device_restore_all(device_save_state_t* dss) {
    assert(dss->save_buffer != NULL);

    QIOChannelBuffer* bioc = qio_channel_buffer_new_external(dss->save_buffer, QEMU_FILE_RAM_LIMIT, dss->save_buffer_size);
    QIOChannel* ioc = QIO_CHANNEL(bioc);

    QEMUFile* f = qemu_file_new_input(ioc);
    
    int save_libafl_restoring_devices = libafl_restoring_devices;
	  libafl_restoring_devices = 1;
    
    qemu_load_device_state(f);
    
    libafl_restoring_devices = save_libafl_restoring_devices;

    qemu_fclose(f);
}

void device_free_all(device_save_state_t* dss) {
    g_free(dss->save_buffer);
}
