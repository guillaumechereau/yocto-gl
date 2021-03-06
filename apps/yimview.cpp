//
// LICENSE:
//
// Copyright (c) 2016 -- 2019 Fabio Pellacini
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//

#include "../yocto/yocto_image.h"
#include "../yocto/yocto_imageio.h"
#include "../yocto/yocto_utils.h"
#include "yocto_opengl.h"
using namespace yocto;

#include "ext/CLI11.hpp"

struct image_stats {
    bbox4f pxl_bounds = {zero4f, zero4f};
    bbox1f lum_bounds = {zero1f, zero1f};
};

struct app_image {
    // original data
    string  filename = "";
    string  outname  = "";
    string  name     = "";
    image4f img      = {};

    // diplay image
    image4f        display = {};
    opengl_texture gl_txt  = {};

    // image stats
    image_stats stats;

    // tonemapping values
    float exposure = 0;
    bool  filmic   = false;
    bool  srgb     = true;

    // computation futures
    atomic<bool> load_done, display_done, stats_done, texture_done;
    thread       load_thread, display_thread, stats_thread, save_thread;
    atomic<bool> display_stop;
    concurrent_queue<image_region> display_queue;
    string                         error_msg = "";

    // viewing properties
    vec2f image_center = zero2f;
    float image_scale  = 1;
    bool  zoom_to_fit  = false;

    // cleanup and stop threads
    ~app_image() {
        display_stop = true;
        if (load_thread.joinable()) load_thread.join();
        if (display_thread.joinable()) display_thread.join();
        if (stats_thread.joinable()) stats_thread.join();
        if (save_thread.joinable()) save_thread.join();
    }
};

struct app_state {
    // images
    deque<app_image> imgs;
    int              img_id = 0;
};

// compute min/max
void update_stats_async(app_image& img) {
    img.stats_done       = false;
    img.stats.pxl_bounds = invalid_bbox4f;
    img.stats.lum_bounds = invalid_bbox1f;
    for (auto& p : img.img) {
        img.stats.pxl_bounds += p;
        img.stats.lum_bounds += vec1f{luminance(p.xyz)};
    }
    img.stats_done = true;
}

void update_display_async(app_image& img) {
    img.display_done = false;
    img.texture_done = false;
    auto regions     = vector<image_region>{};
    make_image_regions(regions, img.img.size());
    parallel_foreach(
        regions,
        [&img](const image_region& region) {
            tonemap_image_region(img.display, region, img.img, img.exposure,
                img.filmic, img.srgb);
            img.display_queue.push(region);
        },
        &img.display_stop);
    img.display_done = true;
}

// load image
void load_image_async(app_image& img) {
    img.load_done    = false;
    img.stats_done   = false;
    img.display_done = false;
    img.texture_done = false;
    img.error_msg    = "";
    img.img          = {};
    try {
        load_image(img.filename, img.img);
    } catch (const std::exception& e) {
        img.error_msg = e.what();
        return;
    }
    img.load_done      = true;
    img.display        = img.img;
    img.display_thread = thread([&img]() { update_display_async(img); });
    img.stats_thread   = thread([&img]() { update_stats_async(img); });
    img.zoom_to_fit    = true;
}

// save an image
void save_image_async(app_image& img) {
    try {
        if (!is_hdr_filename(img.outname)) {
            auto ldr = image4b{};
            float_to_byte(ldr, img.display);
            save_image(img.outname, ldr);
        } else {
            auto aux = image4f{};
            srgb_to_linear(aux, img.display);
            save_image(img.outname, aux);
        }
    } catch (const std::exception& e) {
        img.error_msg = e.what();
    }
}

// add a new image
void add_new_image(app_state& app, const string& filename,
    const string& outname, float exposure = 0, bool filmic = false,
    bool srgb = true) {
    app.imgs.emplace_back();
    auto& img    = app.imgs.back();
    img.filename = filename;
    img.outname  = (outname == "") ? replace_extension(filename, ".display.png")
                                  : outname;
    img.name         = get_filename(filename);
    img.exposure     = exposure;
    img.filmic       = filmic;
    img.srgb         = srgb;
    img.load_done    = false;
    img.display_done = false;
    img.stats_done   = false;
    img.load_thread  = thread([&img]() { load_image_async(img); });
    app.img_id       = (int)app.imgs.size() - 1;
}

void draw_opengl_widgets(const opengl_window& win) {
    auto& app    = *(app_state*)get_opengl_user_pointer(win);
    auto  edited = false;
    begin_opengl_widgets_frame(win);
    if (begin_opengl_widgets_window(win, "yimview")) {
        auto& img = app.imgs.at(app.img_id);
        if (begin_tabbar_opengl_widget(win, "tabs")) {
            if (begin_tabitem_opengl_widget(win, "image")) {
                draw_combobox_opengl_widget(
                    win, "image", app.img_id, app.imgs, false);
                draw_label_opengl_widget(
                    win, "filename", "%s", img.filename.c_str());
                draw_textinput_opengl_widget(win, "outname", img.outname);
                if (draw_button_opengl_widget(win, "save display")) {
                    if (img.display_done) {
                        img.save_thread = thread(
                            [&img]() { save_image_async(img); });
                    }
                }
                auto status = string();
                if (img.error_msg != "")
                    status = "error: " + img.error_msg;
                else if (!img.load_done)
                    status = "loading...";
                else if (!img.display_done)
                    status = "displaying...";
                else
                    status = "done";
                draw_label_opengl_widget(win, "status", status.c_str());
                draw_label_opengl_widget(win, "size", "%d x %d ",
                    img.img.size().x, img.img.size().y);
                draw_slider_opengl_widget(
                    win, "zoom", img.image_scale, 0.1, 10);
                draw_checkbox_opengl_widget(
                    win, "zoom to fit", img.zoom_to_fit);
                end_tabitem_opengl_widget(win);
            }
            if (begin_tabitem_opengl_widget(win, "adjust")) {
                edited += draw_slider_opengl_widget(
                    win, "exposure", img.exposure, -5, 5);
                edited += draw_checkbox_opengl_widget(
                    win, "filmic", img.filmic);
                edited += draw_checkbox_opengl_widget(win, "srgb", img.srgb);
                end_tabitem_opengl_widget(win);
            }
            if (begin_tabitem_opengl_widget(win, "inspect")) {
                auto mouse_pos = get_opengl_mouse_pos(win);
                auto ij        = get_image_coords(mouse_pos, img.image_center,
                    img.image_scale, img.img.size());
                draw_dragger_opengl_widget(win, "mouse", ij);
                auto img_pixel = zero4f, display_pixel = zero4f;
                if (ij.x >= 0 && ij.x < img.img.size().x && ij.y >= 0 &&
                    ij.y < img.img.size().y) {
                    img_pixel     = img.img[{ij.x, ij.y}];
                    display_pixel = img.display[{ij.x, ij.y}];
                }
                draw_coloredit_opengl_widget(win, "pixel color", img_pixel);
                draw_dragger_opengl_widget(win, "pixel value", display_pixel);
                auto stats = (img.stats_done) ? img.stats : image_stats{};
                draw_dragger_opengl_widget(
                    win, "pxl min", stats.pxl_bounds.min);
                draw_dragger_opengl_widget(
                    win, "pxl max", stats.pxl_bounds.max);
                draw_dragger_opengl_widget(
                    win, "lum min", stats.lum_bounds.min);
                draw_dragger_opengl_widget(
                    win, "lum max", stats.lum_bounds.max);
                end_tabitem_opengl_widget(win);
            }
            end_tabbar_opengl_widget(win);
        }
    }
    end_opengl_widgets_frame(win);
    if (edited) {
        auto& img = app.imgs.at(app.img_id);
        if (img.display_thread.joinable()) {
            img.display_stop = true;
            img.display_thread.join();
        }
        if (img.save_thread.joinable()) img.save_thread.join();
        img.display_stop   = false;
        img.display_thread = thread([&img]() { update_display_async(img); });
    }
}

void draw(const opengl_window& win) {
    auto& app      = *(app_state*)get_opengl_user_pointer(win);
    auto& img      = app.imgs.at(app.img_id);
    auto  win_size = get_opengl_window_size(win);
    auto  fb_size  = get_opengl_framebuffer_size(win);
    set_opengl_viewport(fb_size);
    clear_opengl_lframebuffer(vec4f{0.15f, 0.15f, 0.15f, 1.0f});
    if (img.gl_txt) {
        update_image_view(img.image_center, img.image_scale, img.display.size(),
            win_size, img.zoom_to_fit);
        draw_opengl_image_background(img.gl_txt, win_size.x, win_size.y,
            img.image_center, img.image_scale);
        set_opengl_blending(true);
        draw_opengl_image(img.gl_txt, win_size.x, win_size.y, img.image_center,
            img.image_scale);
        set_opengl_blending(false);
    }
    draw_opengl_widgets(win);
    swap_opengl_buffers(win);
}

void update(app_state& app) {
    for (auto& img : app.imgs) {
        if (!img.load_done) continue;
        if (!img.gl_txt) {
            init_opengl_texture(
                img.gl_txt, img.display.size(), false, false, false, false);
        } else {
            auto region = image_region{};
            while (img.display_queue.try_pop(region)) {
                update_opengl_texture_region(
                    img.gl_txt, img.display, region, false);
            }
        }
    }
}

void drop_callback(const opengl_window& win, const vector<string>& paths) {
    auto& app = *(app_state*)get_opengl_user_pointer(win);
    for (auto path : paths) add_new_image(app, path, "");
}

void run_ui(app_state& app) {
    // window
    auto& img = app.imgs.at(app.img_id);
    auto  win = opengl_window();
    init_opengl_window(
        win, {1280, 720}, "yimview | " + app.imgs.front().name, &app, draw);
    set_drop_opengl_callback(win, drop_callback);

    // init widgets
    init_opengl_widgets(win);

    // window values
    auto mouse_pos = zero2f, last_pos = zero2f;
    while (!should_opengl_window_close(win)) {
        last_pos            = mouse_pos;
        mouse_pos           = get_opengl_mouse_pos(win);
        auto mouse_left     = get_opengl_mouse_left(win);
        auto mouse_right    = get_opengl_mouse_right(win);
        auto widgets_active = get_opengl_widgets_active(win);

        // handle mouse
        if (mouse_left && !widgets_active)
            img.image_center += mouse_pos - last_pos;
        if (mouse_right && !widgets_active)
            img.image_scale *= powf(2, (mouse_pos.x - last_pos.x) * 0.001f);

        // update
        update(app);

        // draw
        draw(win);

        // event hadling
        // could also wait if (mouse_left || mouse_right || widgets_active)
        process_opengl_events(win);
    }

    // cleanup
    delete_opengl_window(win);
}

void exit_error(const string& msg) {
    printf("%s\n", msg.c_str());
    exit(1);
}

int main(int argc, char* argv[]) {
    // prepare application
    auto app         = app_state();
    auto exposure    = 0.0f;
    auto filmic      = false;
    auto srgb        = true;
    auto outfilename = ""s;
    auto filenames   = vector<string>{};

    // command line options
    auto parser = CLI::App{"view images"};
    parser.add_option("--exposure,-e", exposure, "display exposure");
    parser.add_flag("--filmic,!--no-filmic", filmic, "display filmic");
    parser.add_flag("--srgb,!--no-srgb", srgb, "display as sRGB");
    // auto quiet = parse_flag(
    //     parser, "--quiet,-q", false, "Print only errors messages");
    parser.add_option("--out,-o", outfilename, "image out filename");
    parser.add_option("images", filenames, "image filenames")->required(true);
    try {
        parser.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return parser.exit(e);
    }

    // loading images
    for (auto filename : filenames)
        add_new_image(app, filename, outfilename, exposure, filmic, srgb);
    app.img_id = 0;

    // run ui
    run_ui(app);

    // done
    return 0;
}
