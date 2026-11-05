// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sim09 contributors
//
// Verilator test bench for the LCD text screen: sends console bytes, runs the system clock (67.5 MHz) and the pixel clock (33.75 MHz, half of it),
// checks the timing of the panel (1192 clocks per line, 548 lines, DE = 800 x 480, negative HSYNC / VSYNC pulses) and writes a frame as a PPM picture (RGB565 widened to 8 bits).
//
//   tb_lcd -i FILE [-o out.ppm] [-f frames] [-g gap]
//     FILE   the bytes to send (a text file with \e, \r, \n, \xHH escapes), sent one every GAP system clocks
//     -f     number of complete frames to run after the bytes are in (default 3); the last one is written
//     -g     GAP (default 150)
//   exit code 0 ok, 1 timing error, 2 usage

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include "Vlcd_top.h"
#include "verilated.h"

static const int W = 800, H = 480, HT = 1192, VT = 548;

int main(int argc, char **argv)
{
    Verilated::commandArgs(argc, argv);
    const char *in = nullptr, *out = nullptr;
    int frames = 3, gap = 150;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-i") && i + 1 < argc) in = argv[++i];
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "-f") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-g") && i + 1 < argc) gap = atoi(argv[++i]);
    }
    if (!in) { fprintf(stderr, "usage: tb_lcd -i FILE [-o out.ppm] [-f frames] [-g gap]\n"); return 2; }
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
                if (s[i] == '\n' && i + 1 == s.size()) continue;      // the final newline of the file
                bytes.push_back((uint8_t)s[i]);
            }
        }
    }

    Vlcd_top *top = new Vlcd_top;
    top->clk = 0; top->clk_pix = 0; top->rst = 1; top->stb = 0; top->din = 0;
    size_t bi = 0; long sysclk = 0, gapcnt = 0;
    int err = 0;
    std::vector<uint8_t> img(W * H * 3, 0), last(W * H * 3, 0);
    // timing checks
    long pclk = 0, line_start = -1, frame_start = -1;
    int x = 0, y = -1; bool de_prev = false, prev_vs = true, prev_hs = true;
    int frames_done = 0, frames_after = -1, timing_err = 0; bool armed = false;      // the checks start at the first complete frame (the first one is cut by the reset)
    long hs_period = 0, last_hs = -1; int hs_width = 0, vs_lines = 0, vs_width_seen = 0;
    long vs_period_lines = 0; int lines_since_vs = 0;
    long de_total = 0; int de_line = 0; int de_lines = 0;
    unsigned step = 0;
    for (;;) {
        step++;
        top->clk = step & 1;                         // the system clock toggles every step, the pixel clock every second step
        bool pix_edge = false;
        if (!(step & 1) == 0 && 0) {}
        if ((step & 3) == 1) { top->clk_pix = 1; pix_edge = true; }
        if ((step & 3) == 3) top->clk_pix = 0;
        top->eval();
        if (step & 1) {
            sysclk++;
            if (sysclk == 40) top->rst = 0;
            top->stb = 0;
            if (sysclk > 400 && bi < bytes.size() && !top->busy && ++gapcnt >= gap) { gapcnt = 0; top->stb = 1; top->din = bytes[bi++]; if (bi == bytes.size()) frames_after = 0; }
        }
        if (pix_edge) {
            // (the outputs after this rising edge belong to the previous clock: sample them before the next one, i.e. now they are from the edge before)
            pclk++;
            bool de = top->lcd_de, hs = top->lcd_hs, vs = top->lcd_vs;
            if (de && !de_prev) { y++; x = 0; de_lines++; }
            if (de) {
                if (y >= 0 && y < H && x < W) {
                    uint8_t *p = &img[(y * W + x) * 3];
                    unsigned r = top->lcd_r, g = top->lcd_g, b = top->lcd_b;
                    p[0] = (uint8_t)((r << 3) | (r >> 2));
                    p[1] = (uint8_t)((g << 2) | (g >> 4));
                    p[2] = (uint8_t)((b << 3) | (b >> 2));
                }
                x++; de_total++;
            } else if (de_prev) {
                if (armed && x != W) { if (timing_err++ < 5) fprintf(stderr, "DE of %d pixels in a line (y=%d)\n", x, y); }
            }
            de_prev = de;
            if (prev_hs && !hs) {                       // the falling edge of HSYNC
                if (getenv("LCDDBG") && lines_since_vs < 12) fprintf(stderr, "hs fall at %ld (period %ld) y=%d vs_lines=%d\n", pclk, pclk - last_hs, y, lines_since_vs);
                if (armed && last_hs >= 0 && pclk - last_hs != HT) { if (timing_err++ < 5) fprintf(stderr, "line of %ld clocks (y=%d)\n", pclk - last_hs, y); }
                last_hs = pclk; hs_width = 0; lines_since_vs++;
            }
            if (!hs) hs_width++;
            if (armed && !prev_hs && hs && hs_width != 1) { if (timing_err++ < 5) fprintf(stderr, "HSYNC pulse of %d clocks\n", hs_width); }
            prev_hs = hs;
            if (prev_vs && !vs) {                       // the frame ends / begins
                if (armed && y == H - 1) { last = img; frames_done++; if (frames_after >= 0) frames_after++; }
                if (armed && lines_since_vs != VT) { if (timing_err++ < 5) fprintf(stderr, "frame of %d lines\n", lines_since_vs); }
                if (lines_since_vs > 500) armed = true;
                vs_period_lines = 1; lines_since_vs = 0; vs_lines = 0;
                y = -1; x = 0; de_prev = false;
            }
            if (!vs) vs_lines++;
            prev_vs = vs;
            if (frames_after >= frames + 1) break;
        }
        if (step > 200000000UL) { fprintf(stderr, "time out: %zu of %zu bytes sent, armed=%d frames_done=%d\n", bi, bytes.size(), (int)armed, frames_done); err = 1; break; }
    }
    (void)frame_start; (void)line_start; (void)hs_period; (void)vs_width_seen; (void)de_total; (void)de_line; (void)de_lines;
    fprintf(stderr, "[%d frames, %d timing errors, %zu bytes sent, screen %dx%d]\n", frames_done, timing_err, bytes.size(), (int)top->scr_cols, (int)top->scr_rows);
    if (timing_err || top->scr_cols != 100 || top->scr_rows != 30) err = 1;
    if (out) {
        FILE *f = fopen(out, "wb");
        fprintf(f, "P6\n%d %d\n255\n", W, H);
        fwrite(last.data(), 1, last.size(), f);
        fclose(f);
    }
    delete top;
    return err;
}
