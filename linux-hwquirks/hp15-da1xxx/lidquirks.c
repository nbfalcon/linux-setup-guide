#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <linux/input.h>

bool strprefix(const char *prefix, const char *str) {
    size_t prefix_len = strlen(prefix);

    // Compare the first prefix_len characters
    return strncmp(prefix, str, prefix_len) == 0;
}

int find_and_open_name(const char *name, int mode) {
    DIR *classInput = opendir("/sys/class/input/");
    if (classInput == NULL) {
        perror("Error opening /sys/class/input/");
        exit(EXIT_FAILURE);
    }

    char *pathbuf = NULL;

    // Read the entries in the directory
    struct dirent *entry;
    while ((entry = readdir(classInput)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0
            || !strprefix("event", entry->d_name)) {
            continue;
        }

        assert(asprintf(&pathbuf, "/sys/class/input/%s/device/name", entry->d_name) >= 0);
        int fdName = open(pathbuf, O_RDONLY);
        if (fdName < 0) {
            fprintf(stderr, "Warning: failed to open %s\n", pathbuf);
            continue;
        }
        char buf[4096];
        if (read(fdName, buf, 4096) < 0) {
            fprintf(stderr, "Read failed on %s\n", pathbuf);
            close(fdName);
            continue;
        }
        close(fdName);
        if (!strprefix(name, buf)) {
            continue;
        }


        assert(asprintf(&pathbuf, "/dev/input/%s", entry->d_name) >= 0);
        int fd = open(pathbuf , mode);
        if (fd < 1) {
            fprintf(stderr, "open(%s) failed: %s\n", pathbuf, strerror(errno));
            free(pathbuf);
            exit(EXIT_FAILURE);
        }
        fprintf(stderr, "Successfully opened '%s' in /dev/input/%s\n", name, entry->d_name);
        return fd;
    }

    free(pathbuf);

    // Close the directory
    closedir(classInput);

    fprintf(stderr, "Error: could not find input device '%s'\n", name);
    exit(EXIT_FAILURE);
}

void setlid(int lidInput, bool open)
{
    printf("Lid: %s\n", open ? "open" : "closed");

    // See libinput source code
    struct input_event ie[2] = {0};
    ie[0].type = EV_SW;
    ie[0].code = SW_LID;
    // SW_LID tells us whether we are open
    ie[0].value = open == false;

    ie[1].type = EV_SYN;
    ie[1].code = SYN_REPORT;
    ie[1].value = 0;

    if (write(lidInput, &ie, sizeof(ie)) < 0) {
        perror("Error: write to lid device failed");
    }
}

int main(void) {
    // Check if the program is run as root
    if (geteuid() != 0) {
        fprintf(stderr, "Error: This program must be run as root.\n");
        exit(EXIT_FAILURE);
    }

    do {
        // These devices may disappear on suspend (Lid close)
        int lidswitchInput = find_and_open_name("Lid Switch", O_WRONLY);
        int atkbdInput = find_and_open_name("AT Translated Set 2 keyboard", O_RDWR);

        struct input_event ev[512];
        int nev;
        while ((nev = read(atkbdInput, &ev, sizeof (ev))) >= 0) {
            nev /= sizeof (struct input_event);
            for (int i = 0; i < nev; i++) {
                // No idea what 0xd7 means
                if (ev[i].type == EV_MSC && ev[i].value == 0xd8) {
                    setlid(lidswitchInput, /* open */ false);
                }
                if (ev[i].type == EV_MSC && ev[i].value == 0xe3) {
                    setlid(lidswitchInput, /* open */ true);
                }
            }
        }

        close(lidswitchInput);
        close(atkbdInput);
    } while (true);
}
