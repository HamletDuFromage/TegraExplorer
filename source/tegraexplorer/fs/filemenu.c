#include <string.h>
#include "entrymenu.h"
#include "../common/common.h"
#include "../../libs/fatfs/ff.h"
#include "../../gfx/gfx.h"
#include "fsutils.h"
#include "fsactions.h"
#include "../utils/utils.h"
#include "../gfx/gfxutils.h"
#include "../../mem/heap.h"
#include "fsreader.h"
#include "../gfx/menu.h"
#include "../common/types.h"
#include "../../utils/sprintf.h"
#include "../script/parser.h"
#include "../emmc/emmcoperations.h"
#include "../../hid/hid.h"

extern char *currentpath;
extern char *clipboard;
extern u8 clipboardhelper;
extern int launch_payload(char *path);

int delfile(const char *path, const char *filename){
    gfx_clearscreen();
    SWAPCOLOR(COLOR_ORANGE);
    gfx_printf("Are you sure you want to delete:\n%s\n\nPress B to cancel\n", filename);
    if (gfx_makewaitmenu("Press A to delete", 2)){
        f_unlink(path);
        fsreader_readfolder(currentpath);
        return 0;
    }
    else
        return -1;
}

void viewbytes(char *path){
    FIL in;
    u8 *print;
    u32 size;
    QWORD offset = 0;
    int res;
    Inputs *input = hidRead();

    while (input->buttons & (KEY_POW | KEY_B))
        hidRead();

    gfx_clearscreen();
    print = malloc (1024);

    if ((res = f_open(&in, path, FA_READ | FA_OPEN_EXISTING))){
        gfx_errDisplay("viewbytes", res, 1);
        return;
    }

    while (1){
        f_lseek(&in, offset * 16);

        if ((res = f_read(&in, print, 1024 * sizeof(u8), &size))){
            gfx_errDisplay("viewbytes", res, 2);
            return;
        }

        gfx_con_setpos(0, 31);
        gfx_hexdump(offset * 16, print, size * sizeof(u8));

        input = hidRead();

        if (!(input->buttons))
            input = hidWait();

        if (input->Ldown && 1024 * sizeof(u8) == size)
            offset++;
        if (input->Lup && offset > 0)
            offset--;
        if (input->buttons & (KEY_POW | KEY_B))
            break;
    }
    f_close(&in);
    free(print);
}

void copyfile(const char *src_in, const char *outfolder){
    char *in, *out, *filename;
    int res;

    gfx_clearscreen();
    utils_copystring(src_in, &in);
    utils_copystring(strrchr(in, '/') + 1, &filename);
    utils_copystring(fsutil_getnextloc(outfolder, filename), &out);

    gfx_printf("Note:\nTo stop the transfer hold Vol-\n\n%s\nProgress: ", filename);

    if (!strcmp(in, out)){
        gfx_errDisplay("gfxcopy", ERR_SAME_LOC, 1);
        return;
    }

    if (clipboardhelper & OPERATIONMOVE){
        if ((res = f_rename(in, out))){
            gfx_errDisplay("gfxcopy", res, 2);
            return;
        }
    }
    else if (clipboardhelper & OPERATIONCOPY) {
        if (fsact_copy(in, out, COPY_MODE_CANCEL | COPY_MODE_PRINT))
            return;
    }

    else {
        gfx_errDisplay("gfxcopy", ERR_EMPTY_CLIPBOARD, 3);
        return;
    }

    free(in);
    free(out);
    free(filename);
    fsreader_readfolder(currentpath);
}

int filemenu(menu_entry file){
    int temp;
    FILINFO attribs;

    for (int i = 0; i < 3; i++)
        if (fs_menu_file[i].name != NULL)
            free(fs_menu_file[i].name);
    
    utils_copystring(file.name, &fs_menu_file[0].name);
    fs_menu_file[1].name = malloc(16);
    fs_menu_file[2].name = malloc(16);
            
    for (temp = 4; temp < 8; temp++)
        if ((file.property & (1 << temp)))
            break;

    
    sprintf(fs_menu_file[1].name, "\nSize: %d %s", file.storage, gfx_file_size_names[temp - 4]);

    if (f_stat(fsutil_getnextloc(currentpath, file.name), &attribs))
        SETBIT(fs_menu_file[2].property, ISHIDE, 1);
    else {
        SETBIT(fs_menu_file[2].property, ISHIDE, 0);
        sprintf(fs_menu_file[2].name, "Attribs: %c%c%c%c",
        (attribs.fattrib & AM_RDO) ? 'R' : '-',
        (attribs.fattrib & AM_SYS) ? 'S' : '-',
        (attribs.fattrib & AM_HID) ? 'H' : '-',
        (attribs.fattrib & AM_ARC) ? 'A' : '-');
    }

    SETBIT(fs_menu_file[6].property, ISHIDE, !hidConnected());
    SETBIT(fs_menu_file[8].property, ISHIDE, !(strstr(file.name, ".bin") != NULL && file.property & ISKB) && strstr(file.name, ".rom") == NULL);
    SETBIT(fs_menu_file[9].property, ISHIDE, strstr(file.name, ".te") == NULL);
    SETBIT(fs_menu_file[11].property, ISHIDE, strstr(file.name, ".bis") == NULL);

    temp = menu_make(fs_menu_file, 12, "-- File Menu --");
    switch (temp){
        case FILE_COPY:
            fsreader_writeclipboard(fsutil_getnextloc(currentpath, file.name), OPERATIONCOPY);
            break;
        case FILE_MOVE:
            fsreader_writeclipboard(fsutil_getnextloc(currentpath, file.name), OPERATIONMOVE);
            break;
        case FILE_DELETE:
            delfile(fsutil_getnextloc(currentpath, file.name), file.name);
            break;
        case FILE_RENAME:;
            char *name, *curPath;
            gfx_clearscreen();
            gfx_printf("Renaming %s...\n\n", file.name);
            name = utils_InputText(file.name, 39);
            if (name == NULL)
                break;
            
            utils_copystring(fsutil_getnextloc(currentpath, file.name), &curPath);

            temp = f_rename(curPath, fsutil_getnextloc(currentpath, name));
            
            free(curPath);
            free(name);

            if (temp){
                gfx_errDisplay("fileMenu", temp, 0);
                break;
            }

            fsreader_readfolder(currentpath);
            break;
        case FILE_PAYLOAD:
            launch_payload(fsutil_getnextloc(currentpath, file.name));
            break;
        case FILE_SCRIPT:
            //ParseScript(fsutil_getnextloc(currentpath, file.name));
            /*
            gfx_printf(" %kRelease any buttons if held!", COLOR_RED);

            while (hidRead()->buttons);
            */

            runScript(fsutil_getnextloc(currentpath, file.name));
            fsreader_readfolder(currentpath);
            break;
        case FILE_HEXVIEW:
            viewbytes(fsutil_getnextloc(currentpath, file.name));
            break;
        case FILE_DUMPBIS:
            gfx_clearscreen();
            extract_bis_file(fsutil_getnextloc(currentpath, file.name), currentpath);
            fsreader_readfolder(currentpath);
            hidWait();
            break;
        case -1:
            return -1;
    }

    return 0;
}
