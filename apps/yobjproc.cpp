//
// LICENSE:
//
// Copyright (c) 2016 -- 2018 Fabio Pellacini
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

#include "../yocto/yocto_scene.h"
#include "../yocto/yocto_sceneio.h"
#include "../yocto/yocto_utils.h"
using namespace yocto;

string to_string(const obj_vertex& v) {
    auto s = std::to_string(v.position);
    if (v.texturecoord) {
        s += "/" + std::to_string(v.texturecoord);
        if (v.normal) s += "/" + std::to_string(v.normal);
    } else {
        if (v.normal) s += "//" + std::to_string(v.normal);
    }
    return s;
}

int main(int argc, char* argv[]) {
    // parse command line
    auto parser = make_cmdline_parser(
        argc, argv, "Process obj files directly", "yobjproc");
    auto translation = parse_argument(
        parser, "--translation,-t", zero3f, "translation");
    auto scale = parse_argument(parser, "--scale,-s", vec3f{1, 1, 1}, "scale");
    auto print_info = parse_argument(
        parser, "--print-info,-i", false, "print obj info");
    auto output = parse_argument(
        parser, "--output,-o", "out.obj"s, "output obj scene", true);
    auto filename = parse_argument(
        parser, "filename", "in.obj"s, "input obj filename", true);
    check_cmdline(parser);

    // prepare stats
    auto npos = 0, nnorm = 0, ntexcoord = 0;
    auto nobjects = 0, ngroups = 0, nusemtl = 0, nmaterials = 0;
    auto nfaces = 0, nplines = 0, nlines = 0, nppoints = 0, npoints = 0;
    auto ntriangles = 0, nquads = 0, npolys = 0;
    auto bbox = invalid_bbox3f;
    auto tbox = invalid_bbox3f;

    // prepare file to output
    auto fs = fopen(output.c_str(), "wt");
    if (!fs) log_fatal("cannot open {}", output);

    // obj callbacks
    auto cb = obj_callbacks();

    // vertex data
    cb.vert = [&](const vec3f& v) {
        auto tv = translation + scale * v;
        fprintf(fs, "v  %6g %6g %6g\n", tv.x, tv.y, tv.z);
        bbox += v;
        tbox += tv;
        npos += 1;
    };
    cb.norm = [&](const vec3f& v) {
        fprintf(fs, "vn %6g %6g %6g\n", v.x, v.y, v.z);
        nnorm += 1;
    };
    cb.texcoord = [&](const vec2f& v) {
        fprintf(fs, "vt %6g %6g\n", v.x, v.y);
        ntexcoord += 1;
    };
    cb.face = [&](const vector<obj_vertex>& verts) {
        fprintf(fs, "f");
        for (auto v : verts) fprintf(fs, " %s", to_string(v).c_str());
        fprintf(fs, "\n");
        nfaces += 1;
        if (verts.size() == 3) ntriangles += 1;
        if (verts.size() == 4) nquads += 1;
        if (verts.size() > 4) npolys += 1;
    };
    cb.line = [&](const vector<obj_vertex>& verts) {
        fprintf(fs, "l");
        for (auto v : verts) fprintf(fs, " %s", to_string(v).c_str());
        fprintf(fs, "\n");
        nplines += 1;
        nlines += (int)verts.size() - 1;
    };
    cb.point = [&](const vector<obj_vertex>& verts) {
        fprintf(fs, "p");
        for (auto v : verts) fprintf(fs, " %s", to_string(v).c_str());
        fprintf(fs, "\n");
        nppoints += 1;
        npoints += (int)verts.size();
    };
    cb.object = [&](const string& name) {
        fprintf(fs, "o %s\n", name.c_str());
        nobjects += 1;
    };
    cb.group = [&](const string& name) {
        fprintf(fs, "g %s\n", name.c_str());
        ngroups += 1;
    };
    cb.usemtl = [&](const string& name) {
        fprintf(fs, "usemtl %s\n", name.c_str());
        nusemtl += 1;
    };
    cb.mtllib = [&](const string& name) {
        fprintf(fs, "mtllib %s\n", name.c_str());
    };
    cb.material = [&](auto&) { nmaterials += 1; };

    // parse and write
    if (!load_obj(filename, cb)) log_fatal("could not load obj " + filename);

    // print info
    if (print_info) {
        auto center = (bbox.max + bbox.min) / 2;
        auto size   = bbox.max - bbox.min;
        cout << "bbox min: " << bbox.min << "\n";
        cout << "bbox max: " << bbox.max << "\n";
        cout << "bbox cen: " << center << "\n";
        cout << "bbox siz: " << size << "\n";
        if (translation != zero3f || scale != vec3f{1, 1, 1}) {
            auto center = (tbox.max + tbox.min) / 2;
            auto size   = tbox.max - tbox.min;
            cout << "tbox min: " << tbox.min << "\n";
            cout << "tbox max: " << tbox.max << "\n";
            cout << "tbox cen: " << center << "\n";
            cout << "tbox siz: " << size << "\n";
        }
        cout << "faces:    " << nfaces << " " << ntriangles << " " << nquads
             << " " << npolys << "\n";
        cout << "lines:    " << nplines << " " << nlines << "\n";
        cout << "point:    " << nppoints << " " << npoints << "\n";
        cout << "objs:     " << nobjects << " " << ngroups << "\n";
        cout << "mats:     " << nmaterials << " " << nusemtl << "\n";
    }

    // done
    fclose(fs);
}
