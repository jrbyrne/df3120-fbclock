
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <png.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>

#include "pngimage.h"
#include "framebuffer.h"


/* Open framebuffer device and mmap it to memory.
 * fb: pointer to framebuffer struct.
 * filename: name of framebuffer device.
 *
 * If successful, sets fb->descriptor and fb->mem_start.
 */
void new_framebuffer(struct framebuffer *fb, char *filename) {
    size_t bytes;
    void *mem_ptr;

    fb->descriptor = open(filename, O_RDWR);
    if (fb->descriptor == -1) {
        perror("Could not open framebuffer device");
        exit(EXIT_FAILURE);
    }

    /* Populate screen info struct for framebuffer. */
    if (ioctl(fb->descriptor, FBIOGET_VSCREENINFO, &(fb->screeninfo))) {
        perror("Unable to get screen info");
        exit(EXIT_FAILURE);
    }

    bytes = screen_size_in_bytes(fb);

    mem_ptr = mmap(NULL, bytes, PROT_READ|PROT_WRITE, MAP_SHARED, 
                   fb->descriptor, 0);

    if (mem_ptr == MAP_FAILED) {
        perror("Failed to mmap framebuffer device to memory");
        exit(EXIT_FAILURE);
    }
    else {
        fb->mem_start = (unsigned char *)mem_ptr;
    }

    return;
}


/* Clean up after we're finished with the framebuffer. Unmap memory
 * and close file descriptor.
 * fb: pointer to framebuffer struct.
 */
void close_framebuffer(struct framebuffer *fb) {
    if (munmap(fb->mem_start, screen_size_in_bytes(fb)) == -1) {
        perror("Failed to munmap framebuffer memory");
        exit(EXIT_FAILURE);
    }
    if (close(fb->descriptor) == -1) {
        perror("Failed to close framebuffer");
        exit(EXIT_FAILURE);
    }
    return;
}



/* Get the size of the screen in bytes.
 * fb: pointer to framebuffer struct.
 * Returns: number of bytes used for the screen.
 */
size_t screen_size_in_bytes(struct framebuffer *fb) {
    size_t bytes;

    bytes = fb->screeninfo.xres
            * fb->screeninfo.yres
            * fb->screeninfo.bits_per_pixel/8;

    /* XXX debug output */
    printf("x resolution: %d\n", fb->screeninfo.xres);
    printf("y resolution: %d\n", fb->screeninfo.yres);
    printf("bpp: %d\n", fb->screeninfo.bits_per_pixel);
    printf("Screen size: %d\n", (int)bytes);

    return bytes;
}


/* display_png: display a PNG file at the given location on the framebuffer.
 * fb: pointer to framebuffer struct.
 * filename: name of the file to display.
 * x_pos, y_pos: x and y coordinates at which to display it on the framebuffer.
 */
struct image_size display_png(struct framebuffer *fb, char *filename, 
                              int x_pos, int y_pos) {
    png_byte header[8];
    FILE *fp;
    png_structp png_ptr;
    png_infop info_ptr;
    png_bytepp row_pointers;

    struct image_size png_size;
    int x, y;
    unsigned char *mem_ptr;

    fp = fopen(filename, "rb");
    if (!fp) {
        perror("Failed to open file");
        exit(EXIT_FAILURE);
    }

    fread(header, 1, 8, fp);
    if (png_sig_cmp(header, 0, 8)) {
        fprintf(stderr, "%s is not a valid PNG file.\n", filename);
        exit(EXIT_FAILURE);
    }

    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    info_ptr = png_create_info_struct(png_ptr);
    if (!png_ptr || !info_ptr) {
        perror("Failed to create PNG data structures");
        exit(EXIT_FAILURE);
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        perror("setjmp failed");
        exit(EXIT_FAILURE);
    }

    png_init_io(png_ptr, fp);
    png_set_sig_bytes(png_ptr, 8);
    png_read_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);

    png_size.x = png_get_image_width(png_ptr, info_ptr);
    png_size.y = png_get_image_height(png_ptr, info_ptr);

    row_pointers = png_get_rows(png_ptr, info_ptr);

    /******** Process and display the image data ********/
    /* XXX TODO: support fb colour depths other than 16-bit */

    /* Move to correct X/Y starting position */
    mem_ptr = fb->mem_start;
    mem_ptr += x_pos * (fb->screeninfo.bits_per_pixel/8);
    mem_ptr += y_pos * (fb->screeninfo.bits_per_pixel/8) * fb->screeninfo.xres;
    /* Loop through image data and write each pixel to the framebuffer */
    if (fb->screeninfo.bits_per_pixel == 16) {
        for (y = 0; y < png_size.y; y++) {
            png_bytep row = row_pointers[y];
            for (x = 0; x < png_size.x; x++) {
                /* Convert 8888 RGBA to 565 RGB */
                png_bytep png_pixel = &(row[x*4]);
                png_byte r = png_pixel[0];
                png_byte g = png_pixel[1];
                png_byte b = png_pixel[2];
                uint16_t fb_pixel = 0;

                r = r >> 3;
                g = g >> 2;
                b = b >> 3;

                fb_pixel = fb_pixel | r;
                fb_pixel = fb_pixel << 6;
                fb_pixel = fb_pixel | g;
                fb_pixel = fb_pixel << 5;
                fb_pixel = fb_pixel | b;

                /* Write pixel to framebuffer */
                *mem_ptr = fb_pixel;
                mem_ptr++;
                *mem_ptr = (fb_pixel >> 8);
                mem_ptr++;
            }
            /* Move back to starting X coordinate */
            mem_ptr -= png_size.x * (fb->screeninfo.bits_per_pixel/8);
            /* Move down to the next row of pixels */
            mem_ptr += (fb->screeninfo.bits_per_pixel/8) * fb->screeninfo.xres;
        }
    }
    return png_size;
}
