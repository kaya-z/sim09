// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sim09 contributors
//
// Verilator test bench for the SPI screen (ILI9341): sends console bytes, runs the system clock (67.5 MHz) and the pixel clock (33.75 MHz, half of it), waits until the
// panel is started and the painter has gone quiet, checks the start sequence and the datasheet counters of the model (ili9341_model.v), and writes the frame memory of the
// panel (320 x 240 RGB565) as a PPM picture.
//
//   tb_spilcd -i FILE [-o out.ppm] [-g gap] [-m max_ms]
//     FILE   the bytes to send (a text file with \e, \r, \n, \xHH escapes), sent one every GAP system clocks
//     -g     GAP (default 150)       -m  give up after this many ms of simulated time (default 700)
//   exit code 0 ok, 1 an error (start sequence, datasheet counters, time out), 2 usage

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include "Vspilcd_top.h"
#include "verilated.h"

static const int W = 320, H = 240;

int main(int argc, char **argv)
{
    Verilated::commandArgs(argc, argv);
    const char *in = nullptr, *out = nullptr;
    int gap = 150; long maxms = 700;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-i") && i + 1 < argc) in = argv[++i];
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "-g") && i + 1 < argc) gap = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-m") && i + 1 < argc) maxms = atol(argv[++i]);
    }
    if (!in) { fprintf(stderr, "usage: tb_spilcd -i FILE [-o out.ppm] [-g gap] [-m max_ms]\n"); return 2; }
    std::vector<uint8_t> bytes;
    {
        FILE *f = fopen(in, "rb");
        if (!f) { perror(in); return 2; }
        int c;
        std::string s;
        while ((c = fgetc(f)) != EOF) s.push_back((char)c);
        fclose(f);
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == '\\' && i + 1 < s.size()) {
                char n = s[++i];
                if (n == 'e') bytes.push_back(27);
                else if (n == 'r') bytes.push_back(13);
                else if (n == 'n') bytes.push_back(10);
                else if (n == 't') bytes.push_back(9);
                else if (n == 'b') bytes.push_back(8);
                else if (n == 'f') bytes.push_back(12);
                else if (n == 'x' && i + 2 < s.size()) { bytes.push_back((uint8_t)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16)); i += 2; }
                else bytes.push_back((uint8_t)n);
            } else {
                if (s[i] == '\n' && i + 1 == s.size()) continue;
                bytes.push_back((uint8_t)s[i]);
            }
        }
    }

    Vspilcd_top *top = new Vspilcd_top;
    top->clk = 0; top->clk_pix = 0; top->rst = 1; top->stb = 0; top->din = 0; top->gram_a = 0;
    size_t bi = 0; long sysclk = 0, gapcnt = 0;
    int err = 0;
    unsigned step = 0;
    long pclk = 0, stable_since = 0, started_at = -1; unsigned last_cells = 0; bool bytes_done = false; long bytes_done_at = 0;
    const long max_pclk = maxms * 33750;
    for (;;) {
        step++;
        top->clk = step & 1;
        bool pix_edge = false;
        if ((step & 3) == 1) { top->clk_pix = 1; pix_edge = true; }
        if ((step & 3) == 3) top->clk_pix = 0;
        top->eval();
        if (step & 1) {
            sysclk++;
            if (sysclk == 40) top->rst = 0;
            top->stb = 0;
            if (sysclk > 400 && bi < bytes.size() && !top->busy && ++gapcnt >= gap) { gapcnt = 0; top->stb = 1; top->din = bytes[bi++]; if (bi == bytes.size()) { bytes_done = true; bytes_done_at = pclk; } }
        }
        if (pix_edge) {
            pclk++;
            if (top->started && started_at < 0) { started_at = pclk; stable_since = pclk; }
            if (top->cells_painted != last_cells) { last_cells = top->cells_painted; stable_since = pclk; }
            // done: the bytes are in, the panel is started, and nothing was painted for 3 ms (the blinking cursor comes every 0.5 s)
            if (bytes_done && top->started && pclk - stable_since > 101250 && pclk - bytes_done_at > 101250) break;
            if (pclk > max_pclk) { fprintf(stderr, "time out after %ld ms: %zu of %zu bytes sent, started=%d cells=%u\n", maxms, bi, bytes.size(), (int)top->started, last_cells); err = 1; break; }
        }
    }
    // the picture in the frame memory of the panel
    std::vector<uint8_t> img(W * H * 3, 0);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            top->gram_a = y * W + x; top->eval();
            unsigned v = top->gram_q;
            unsigned r = v >> 11, g = (v >> 5) & 63, b = v & 31;
            uint8_t *p = &img[(y * W + x) * 3];
            p[0] = (uint8_t)((r << 3) | (r >> 2)); p[1] = (uint8_t)((g << 2) | (g >> 4)); p[2] = (uint8_t)((b << 3) | (b >> 2));
        }
    static const uint8_t expect[7] = {0x01, 0x11, 0x3a, 0x36, 0x29, 0x2a, 0x2b};
    int seq_ok = 1;
    for (int i = 0; i < 7; i++) if (((top->seq >> (8 * i)) & 0xff) != expect[i]) seq_ok = 0;
    double ms = pclk / 33750.0;
    fprintf(stderr, "[%.1f ms, %zu bytes sent, panel on=%d started=%d, cells painted %u, commands %u, pixels %u, madctl %02x colmod %02x, start sequence %s]\n",
            ms, bytes.size(), (int)top->disp_on, (int)top->started, (unsigned)top->cells_painted, (unsigned)top->n_cmd, (unsigned)top->n_pix, (unsigned)top->madctl, (unsigned)top->colmod, seq_ok ? "ok" : "WRONG");
    fprintf(stderr, "[datasheet checks: SCL faster than 100 ns %u times (information), setup %u, early command %u, pixel format %u, state %u; screen %dx%d]\n",
            (unsigned)top->fast, (unsigned)top->bad_setup, (unsigned)top->bad_early, (unsigned)top->bad_format, (unsigned)top->bad_state, (int)top->scr_cols, (int)top->scr_rows);
    if (!seq_ok || top->bad_setup || top->bad_early || top->bad_format || top->bad_state || !top->disp_on || top->scr_cols != 40 || top->scr_rows != 15) err = 1;
    if (out) {
        FILE *f = fopen(out, "wb");
        fprintf(f, "P6\n%d %d\n255\n", W, H);
        fwrite(img.data(), 1, img.size(), f);
        fclose(f);
    }
    delete top;
    return err;
}
