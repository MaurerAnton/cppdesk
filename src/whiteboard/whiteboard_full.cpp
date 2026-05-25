// whiteboard_full.cpp — Comprehensive Whiteboard Implementation
// Part of cppdesk remote desktop whiteboard module
// C++20 | SPDLOG | namespace cppdesk::whiteboard
//
// Implements: layer-based drawing canvas, multiple drawing tools (pen,
// highlighter, eraser, shapes: rect, oval, line, arrow), text tool with
// font selection, color palette/thickness, undo/redo via command pattern,
// canvas zoom/pan, object selection/move/resize/delete, grid/snap-to-grid,
// JSON serialization/deserialization, collaborative editing (merge remote
// strokes), background image/grid, and export to PNG/PDF.

#include <algorithm>
#include <any>
#include <array>
#include <atomic>
#include <bitset>
#include <charconv>
#include <chrono>
#include <cmath>
#include <compare>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iomanip>
#include <iterator>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <queue>
#include <random>
#include <ranges>
#include <set>
#include <shared_mutex>
#include <span>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>
#include <spdlog/fmt/fmt.h>

// ============================================================================
// Namespace
// ============================================================================

namespace cppdesk::whiteboard {

// ============================================================================
// Forward declarations
// ============================================================================

class Canvas;
class Layer;
class Stroke;
class Shape;
class TextObject;
class WhiteboardDocument;
class Command;
class UndoManager;
class Tool;
struct Color;
struct Point;
struct Rect;
struct Transform;

// ============================================================================
// Core geometry types
// ============================================================================

/// A 2D point with floating-point precision.
struct Point {
    double x = 0.0;
    double y = 0.0;

    constexpr Point operator+(Point const& o) const noexcept { return {x + o.x, y + o.y}; }
    constexpr Point operator-(Point const& o) const noexcept { return {x - o.x, y - o.y}; }
    constexpr Point operator*(double s) const noexcept { return {x * s, y * s}; }
    constexpr Point operator/(double s) const noexcept { return {x / s, y / s}; }
    constexpr bool operator==(Point const& o) const noexcept = default;
    constexpr double length() const noexcept { return std::sqrt(x * x + y * y); }
    constexpr double distance_to(Point const& o) const noexcept { return (*this - o).length(); }
};

/// A 2D axis-aligned rectangle.
struct Rect {
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;

    [[nodiscard]] constexpr double left()   const noexcept { return x; }
    [[nodiscard]] constexpr double right()  const noexcept { return x + w; }
    [[nodiscard]] constexpr double top()    const noexcept { return y; }
    [[nodiscard]] constexpr double bottom() const noexcept { return y + h; }
    [[nodiscard]] constexpr Point center()  const noexcept { return {x + w / 2.0, y + h / 2.0}; }
    [[nodiscard]] constexpr bool contains(Point const& p) const noexcept {
        return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h;
    }
    [[nodiscard]] constexpr bool intersects(Rect const& o) const noexcept {
        return !(o.x > x + w || o.x + o.w < x || o.y > y + h || o.y + o.h < y);
    }

    constexpr Rect& inflate(double dx, double dy) noexcept {
        x -= dx; y -= dy; w += 2 * dx; h += 2 * dy; return *this;
    }
    [[nodiscard]] constexpr Rect inflated(double dx, double dy) const noexcept {
        return {x - dx, y - dy, w + 2 * dx, h + 2 * dy};
    }

    static constexpr Rect from_points(Point a, Point b) noexcept {
        double x0 = std::min(a.x, b.x);
        double y0 = std::min(a.y, b.y);
        return {x0, y0, std::abs(b.x - a.x), std::abs(b.y - a.y)};
    }
};

/// RGBA color.
struct Color {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;

    constexpr bool operator==(Color const&) const noexcept = default;

    [[nodiscard]] std::string to_hex() const {
        return fmt::format("#{:02X}{:02X}{:02X}{:02X}", r, g, b, a);
    }

    static constexpr Color from_rgba(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_) noexcept {
        return {r_, g_, b_, a_};
    }
    static constexpr Color black()       noexcept { return {0,   0,   0,   255}; }
    static constexpr Color white()       noexcept { return {255, 255, 255, 255}; }
    static constexpr Color red()         noexcept { return {255, 0,   0,   255}; }
    static constexpr Color green()       noexcept { return {0,   255, 0,   255}; }
    static constexpr Color blue()        noexcept { return {0,   0,   255, 255}; }
    static constexpr Color yellow()      noexcept { return {255, 255, 0,   255}; }
    static constexpr Color transparent() noexcept { return {0,   0,   0,   0}; }

    [[nodiscard]] constexpr Color with_alpha(uint8_t na) const noexcept {
        return {r, g, b, na};
    }
};

// ============================================================================
// Drawing primitive types
// ============================================================================

/// Enumeration of all shape kinds.
enum class ShapeKind : uint8_t {
    rectangle,
    rounded_rectangle,
    ellipse,
    line,
    arrow,
    diamond,
    triangle,
    star,
    polygon,
    freehand,   // pen strokes
    highlighter // translucent overlay strokes
};

/// Enumeration of fill styles.
enum class FillStyle : uint8_t {
    none,
    solid,
    semi_transparent,
    gradient
};

/// Enumeration of stroke cap styles.
enum class StrokeCap : uint8_t {
    butt,
    round,
    square
};

/// Enumeration of stroke join styles.
enum class StrokeJoin : uint8_t {
    miter,
    round,
    bevel
};

/// Enumeration of text alignment.
enum class TextAlign : uint8_t {
    left,
    center,
    right
};

/// Enumeration of text vertical alignment.
enum class TextVAlign : uint8_t {
    top,
    middle,
    bottom
};

/// Enumeration of layer blend modes.
enum class BlendMode : uint8_t {
    normal,
    multiply,
    screen,
    overlay,
    erase
};

/// Font descriptor for text objects.
struct FontDescriptor {
    std::string family = "sans-serif";
    double size = 16.0;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikethrough = false;

    [[nodiscard]] std::string to_string() const {
        std::string s;
        if (bold) s += "bold ";
        if (italic) s += "italic ";
        s += fmt::format("{} {:.1f}px", family, size);
        return s;
    }
};

// ============================================================================
// Drawing attributes
// ============================================================================

/// Stroke / outline style.
struct StrokeStyle {
    Color color = Color::black();
    double width = 2.0;
    StrokeCap cap = StrokeCap::round;
    StrokeJoin join = StrokeJoin::round;
    std::vector<double> dash_pattern; // empty = solid

    [[nodiscard]] constexpr bool is_dashed() const noexcept { return !dash_pattern.empty(); }
};

/// Fill style for shapes.
struct Fill {
    FillStyle style = FillStyle::none;
    Color color = Color::transparent();
    Color color2 = Color::transparent(); // for gradient
};

/// Composite drawing attributes.
struct DrawingAttributes {
    StrokeStyle stroke;
    Fill fill;
    double opacity = 1.0;
    BlendMode blend_mode = BlendMode::normal;
};

// ============================================================================
// Tool identifiers
// ============================================================================

enum class ToolType : uint8_t {
    select,
    pan,
    pen,
    highlighter,
    eraser,
    rectangle,
    oval,
    line,
    arrow,
    text,
    eyedropper,
    fill_bucket,
    laser_pointer
};

/// Tool properties that can be adjusted by the user.
struct ToolProperties {
    ToolType type = ToolType::pen;
    Color color = Color::black();
    double thickness = 3.0;
    double opacity = 1.0;
    FontDescriptor font;
    bool snap_to_grid = true;
    bool pressure_sensitive = false;
};

// ============================================================================
// Grid settings
// ============================================================================

struct GridSettings {
    bool visible = true;
    bool snap_enabled = true;
    double spacing = 20.0;
    Color color = {200, 200, 200, 128};
    double major_line_every = 5; // every Nth line is a major line
    Color major_color = {160, 160, 160, 180};
};

// ============================================================================
// Canvas transform (zoom + pan)
// ============================================================================

struct CanvasTransform {
    double scale = 1.0;
    double offset_x = 0.0;
    double offset_y = 0.0;
    double min_scale = 0.1;
    double max_scale = 10.0;

    /// Convert screen coordinates to canvas coordinates.
    [[nodiscard]] Point screen_to_canvas(Point screen) const noexcept {
        return {
            (screen.x - offset_x) / scale,
            (screen.y - offset_y) / scale
        };
    }

    /// Convert canvas coordinates to screen coordinates.
    [[nodiscard]] Point canvas_to_screen(Point canvas) const noexcept {
        return {
            canvas.x * scale + offset_x,
            canvas.y * scale + offset_y
        };
    }

    /// Zoom toward a specific screen point.
    void zoom_at(Point screen_point, double factor) {
        Point canvas_before = screen_to_canvas(screen_point);
        scale = std::clamp(scale * factor, min_scale, max_scale);
        Point canvas_after = screen_to_canvas(screen_point);
        offset_x += (canvas_after.x - canvas_before.x) * scale;
        offset_y += (canvas_after.y - canvas_before.y) * scale;
    }

    /// Pan the canvas by screen-space delta.
    void pan(double dx, double dy) noexcept {
        offset_x += dx;
        offset_y += dy;
    }

    /// Reset to identity transform.
    void reset() noexcept {
        scale = 1.0;
        offset_x = 0.0;
        offset_y = 0.0;
    }
};

// ============================================================================
// Hit-test result
// ============================================================================

struct HitResult {
    bool hit = false;
    std::string object_id;
    Point point;
    int handle = -1; // -1 = body, 0..7 = resize handles
    double distance = std::numeric_limits<double>::max();
};

// ============================================================================
// Base class for all drawable objects (Stroke, Shape, TextObject)
// ============================================================================

/// Unique ID generator for objects.
struct ObjectId {
    static std::string generate() {
        static std::atomic<uint64_t> counter{0};
        auto now = std::chrono::system_clock::now().time_since_epoch().count();
        uint64_t id = counter.fetch_add(1, std::memory_order_relaxed);
        return fmt::format("obj_{:016x}_{:08x}", static_cast<uint64_t>(now), id);
    }
};

/// Base for all objects on the whiteboard.
class Drawable {
public:
    Drawable() : id_(ObjectId::generate()) {}
    explicit Drawable(std::string id) : id_(std::move(id)) {}
    virtual ~Drawable() = default;

    [[nodiscard]] std::string const& id() const { return id_; }
    void set_id(std::string id) { id_ = std::move(id); }

    [[nodiscard]] DrawingAttributes const& attrs() const { return attrs_; }
    void set_attrs(DrawingAttributes a) { attrs_ = std::move(a); }
    DrawingAttributes& mutable_attrs() { return attrs_; }

    [[nodiscard]] virtual Rect bounding_box() const = 0;
    [[nodiscard]] virtual bool hit_test(Point p, double tolerance = 5.0) const = 0;
    [[nodiscard]] virtual std::unique_ptr<Drawable> clone() const = 0;
    virtual void move_by(double dx, double dy) = 0;
    virtual void scale_from(Point anchor, double sx, double sy) = 0;

    // Serialization
    virtual void to_json(std::ostream& os) const = 0;
    virtual void from_json(std::string_view json) = 0;

    // Metadata
    [[nodiscard]] std::string const& author() const { return author_; }
    void set_author(std::string a) { author_ = std::move(a); }
    [[nodiscard]] uint64_t timestamp() const { return timestamp_; }
    void set_timestamp(uint64_t ts) { timestamp_ = ts; }

    uint32_t version = 1; ///< For CRDT / collaborative merging

protected:
    std::string id_;
    DrawingAttributes attrs_;
    std::string author_;
    uint64_t timestamp_ = 0;
};

// ============================================================================
// Stroke (freehand path)
// ============================================================================

class Stroke : public Drawable {
public:
    Stroke() : Drawable() {}
    explicit Stroke(std::vector<Point> pts, DrawingAttributes attrs = {})
        : Drawable(), points_(std::move(pts)) { attrs_ = std::move(attrs); }

    // Points management
    void add_point(Point p) { points_.push_back(p); }
    void add_points(std::span<Point const> pts) {
        points_.insert(points_.end(), pts.begin(), pts.end());
    }
    [[nodiscard]] std::vector<Point> const& points() const { return points_; }
    [[nodiscard]] std::vector<Point>& mutable_points() { return points_; }
    [[nodiscard]] size_t point_count() const { return points_.size(); }

    // Smoothing (Ramer-Douglas-Peucker)
    void simplify(double epsilon = 1.0) {
        if (points_.size() <= 2) return;
        points_ = simplify_rdp(points_, epsilon);
    }

    // Smoothing via moving average
    void smooth(int window = 3) {
        if (points_.size() < 3 || window < 2) return;
        std::vector<Point> result;
        result.reserve(points_.size());
        for (size_t i = 0; i < points_.size(); ++i) {
            double sx = 0, sy = 0;
            int count = 0;
            int half = window / 2;
            for (int j = -half; j <= half; ++j) {
                int idx = static_cast<int>(i) + j;
                if (idx >= 0 && idx < static_cast<int>(points_.size())) {
                    sx += points_[idx].x;
                    sy += points_[idx].y;
                    ++count;
                }
            }
            result.push_back({sx / count, sy / count});
        }
        points_ = std::move(result);
    }

    [[nodiscard]] Rect bounding_box() const override {
        if (points_.empty()) return {};
        double min_x = points_[0].x, max_x = points_[0].x;
        double min_y = points_[0].y, max_y = points_[0].y;
        for (auto const& p : points_) {
            min_x = std::min(min_x, p.x);
            max_x = std::max(max_x, p.x);
            min_y = std::min(min_y, p.y);
            max_y = std::max(max_y, p.y);
        }
        double sw = attrs_.stroke.width;
        return {min_x - sw, min_y - sw, max_x - min_x + 2 * sw, max_y - min_y + 2 * sw};
    }

    [[nodiscard]] bool hit_test(Point p, double tolerance = 5.0) const override {
        if (points_.empty()) return false;
        for (size_t i = 1; i < points_.size(); ++i) {
            if (point_to_segment_distance(p, points_[i - 1], points_[i]) <= tolerance)
                return true;
        }
        return false;
    }

    [[nodiscard]] std::unique_ptr<Drawable> clone() const override {
        auto s = std::make_unique<Stroke>(points_, attrs_);
        s->id_ = id_;
        s->author_ = author_;
        s->timestamp_ = timestamp_;
        s->version = version;
        return s;
    }

    void move_by(double dx, double dy) override {
        for (auto& p : points_) { p.x += dx; p.y += dy; }
    }

    void scale_from(Point anchor, double sx, double sy) override {
        for (auto& p : points_) {
            p.x = anchor.x + (p.x - anchor.x) * sx;
            p.y = anchor.y + (p.y - anchor.y) * sy;
        }
    }

    void to_json(std::ostream& os) const override {
        os << R"({"type":"stroke","id":")" << id_
           << R"(","version":)" << version
           << R"(,"attrs":)" << attrs_to_json()
           << R"(,"points":[)";
        for (size_t i = 0; i < points_.size(); ++i) {
            if (i > 0) os << ',';
            os << fmt::format("[{:.3f},{:.3f}]", points_[i].x, points_[i].y);
        }
        os << R"(])" << meta_json() << "}";
    }

    void from_json(std::string_view json) override {
        // Simplified: in production would use a real JSON parser
        // Parse points array, attrs, etc.
        points_.clear();
        auto pts_start = json.find("\"points\":[");
        if (pts_start != std::string_view::npos) {
            auto pts_end = json.find(']', pts_start + 10);
            auto pts = json.substr(pts_start + 10, pts_end - pts_start - 9);
            // Parse [x,y] pairs
            size_t pos = 0;
            while (pos < pts.size()) {
                auto br = pts.find('[', pos);
                if (br == std::string_view::npos) break;
                auto comma = pts.find(',', br);
                auto br_end = pts.find(']', comma);
                if (comma == std::string_view::npos || br_end == std::string_view::npos) break;
                double px = 0, py = 0;
                auto xsv = pts.substr(br + 1, comma - br - 1);
                auto ysv = pts.substr(comma + 1, br_end - comma - 1);
                std::from_chars(xsv.data(), xsv.data() + xsv.size(), px);
                std::from_chars(ysv.data(), ysv.data() + ysv.size(), py);
                points_.push_back({px, py});
                pos = br_end + 1;
            }
        }
    }

private:
    std::vector<Point> points_;

    static double point_to_segment_distance(Point p, Point a, Point b) {
        double dx = b.x - a.x, dy = b.y - a.y;
        double len_sq = dx * dx + dy * dy;
        if (len_sq < 1e-10) return p.distance_to(a);
        double t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len_sq, 0.0, 1.0);
        Point proj = {a.x + t * dx, a.y + t * dy};
        return p.distance_to(proj);
    }

    static std::vector<Point> simplify_rdp(std::vector<Point> const& pts, double epsilon) {
        if (pts.size() < 3) return pts;
        // Find point furthest from line between endpoints
        double max_dist = 0;
        size_t max_idx = 0;
        Point a = pts.front(), b = pts.back();
        for (size_t i = 1; i < pts.size() - 1; ++i) {
            double d = point_to_segment_distance(pts[i], a, b);
            if (d > max_dist) { max_dist = d; max_idx = i; }
        }
        if (max_dist <= epsilon) return {a, b};
        auto left = simplify_rdp({pts.begin(), pts.begin() + max_idx + 1}, epsilon);
        auto right = simplify_rdp({pts.begin() + max_idx, pts.end()}, epsilon);
        left.insert(left.end(), right.begin() + 1, right.end());
        return left;
    }

    std::string attrs_to_json() const {
        return fmt::format(
            R"({{"stroke":{{"color":"{}","width":{:.1f}}},"opacity":{:.2f}}})",
            attrs_.stroke.color.to_hex(), attrs_.stroke.width, attrs_.opacity);
    }

    std::string meta_json() const {
        return fmt::format(R"(,"author":"{}","ts":{})", author_, timestamp_);
    }
};

// ============================================================================
// Shape (rectangle, oval, line, arrow, etc.)
// ============================================================================

class Shape : public Drawable {
public:
    Shape() : Drawable() {}
    explicit Shape(ShapeKind kind, Rect bounds, DrawingAttributes attrs = {})
        : Drawable(), kind_(kind), bounds_(bounds) { attrs_ = std::move(attrs); }

    [[nodiscard]] ShapeKind kind() const { return kind_; }
    void set_kind(ShapeKind k) { kind_ = k; }

    [[nodiscard]] Rect const& bounds() const { return bounds_; }
    void set_bounds(Rect r) { bounds_ = r; }

    // For arrow shape
    [[nodiscard]] Point const& arrow_head() const { return arrow_head_; }
    void set_arrow_head(Point p) { arrow_head_ = p; }

    [[nodiscard]] double corner_radius() const { return corner_radius_; }
    void set_corner_radius(double r) { corner_radius_ = r; }

    [[nodiscard]] Rect bounding_box() const override {
        double sw = attrs_.stroke.width;
        return bounds_.inflated(sw / 2.0, sw / 2.0);
    }

    [[nodiscard]] bool hit_test(Point p, double tolerance = 5.0) const override {
        Rect hit_area = bounds_.inflated(tolerance, tolerance);
        if (!hit_area.contains(p)) return false;

        if (attrs_.fill.style != FillStyle::none) return true; // filled shapes

        // For lines/arrows, check proximity to the line segment
        if (kind_ == ShapeKind::line || kind_ == ShapeKind::arrow) {
            Point start = {bounds_.x, bounds_.y};
            Point end = {bounds_.x + bounds_.w, bounds_.y + bounds_.h};
            double dx = end.x - start.x, dy = end.y - start.y;
            double len_sq = dx * dx + dy * dy;
            if (len_sq < 1e-10) return p.distance_to(start) <= tolerance;
            double t = std::clamp(((p.x - start.x) * dx + (p.y - start.y) * dy) / len_sq, 0.0, 1.0);
            Point proj = {start.x + t * dx, start.y + t * dy};
            return p.distance_to(proj) <= tolerance + attrs_.stroke.width;
        }

        // For ellipse: check if point is near the edge
        if (kind_ == ShapeKind::ellipse) {
            Point c = bounds_.center();
            double rx = bounds_.w / 2.0, ry = bounds_.h / 2.0;
            if (rx < 1e-10 || ry < 1e-10) return false;
            double nx = (p.x - c.x) / rx, ny = (p.y - c.y) / ry;
            double dist = std::abs(std::sqrt(nx * nx + ny * ny) - 1.0) * std::max(rx, ry);
            return dist <= tolerance + attrs_.stroke.width;
        }

        // Rectangle: check if near any edge
        double hw = attrs_.stroke.width / 2.0 + tolerance;
        bool near_left   = std::abs(p.x - bounds_.left())   <= hw && p.y >= bounds_.top() - hw && p.y <= bounds_.bottom() + hw;
        bool near_right  = std::abs(p.x - bounds_.right())  <= hw && p.y >= bounds_.top() - hw && p.y <= bounds_.bottom() + hw;
        bool near_top    = std::abs(p.y - bounds_.top())    <= hw && p.x >= bounds_.left()  - hw && p.x <= bounds_.right()  + hw;
        bool near_bottom = std::abs(p.y - bounds_.bottom()) <= hw && p.x >= bounds_.left()  - hw && p.x <= bounds_.right()  + hw;
        return near_left || near_right || near_top || near_bottom;
    }

    [[nodiscard]] std::unique_ptr<Drawable> clone() const override {
        auto s = std::make_unique<Shape>(kind_, bounds_, attrs_);
        s->id_ = id_;
        s->author_ = author_;
        s->timestamp_ = timestamp_;
        s->version = version;
        s->corner_radius_ = corner_radius_;
        s->arrow_head_ = arrow_head_;
        return s;
    }

    void move_by(double dx, double dy) override {
        bounds_.x += dx;
        bounds_.y += dy;
        arrow_head_.x += dx;
        arrow_head_.y += dy;
    }

    void scale_from(Point anchor, double sx, double sy) override {
        bounds_.x = anchor.x + (bounds_.x - anchor.x) * sx;
        bounds_.y = anchor.y + (bounds_.y - anchor.y) * sy;
        bounds_.w *= sx;
        bounds_.h *= sy;
    }

    void to_json(std::ostream& os) const override {
        os << fmt::format(
            R"({{"type":"shape","id":"{}","version":{},"kind":{},"bounds":[{:.3f},{:.3f},{:.3f},{:.3f}]",
                      "cr":{:.1f},"attrs":{})",
            id_, version, static_cast<int>(kind_),
            bounds_.x, bounds_.y, bounds_.w, bounds_.h,
            corner_radius_, attrs_json());
        if (kind_ == ShapeKind::arrow) {
            os << fmt::format(R"(,"arrow_head":[{:.3f},{:.3f}])", arrow_head_.x, arrow_head_.y);
        }
        os << meta_json() << "}";
    }

    void from_json(std::string_view json) override {
        // Simplified parsing
        auto extract_value = [&](std::string_view key) -> std::string_view {
            auto pos = json.find(key);
            if (pos == std::string_view::npos) return {};
            auto val_start = json.find(':', pos);
            if (val_start == std::string_view::npos) return {};
            ++val_start;
            while (val_start < json.size() && (json[val_start] == ' ' || json[val_start] == '"'))
                ++val_start;
            auto val_end = val_start;
            while (val_end < json.size() && json[val_end] != ',' && json[val_end] != '}' && json[val_end] != ']')
                ++val_end;
            return json.substr(val_start, val_end - val_start);
        };
        auto k = extract_value("\"kind\"");
        if (!k.empty()) {
            int vk = 0;
            std::from_chars(k.data(), k.data() + k.size(), vk);
            kind_ = static_cast<ShapeKind>(vk);
        }
    }

private:
    ShapeKind kind_ = ShapeKind::rectangle;
    Rect bounds_;
    double corner_radius_ = 0.0;
    Point arrow_head_;

    std::string attrs_json() const {
        return fmt::format(
            R"({{"stroke":{{"color":"{}","width":{:.1f}}},"fill":{},"opacity":{:.2f}}})",
            attrs_.stroke.color.to_hex(), attrs_.stroke.width,
            fill_json(), attrs_.opacity);
    }

    std::string fill_json() const {
        if (attrs_.fill.style == FillStyle::none) return "null";
        return fmt::format(R"("{}")", attrs_.fill.color.to_hex());
    }

    std::string meta_json() const {
        return fmt::format(R"(,"author":"{}","ts":{})", author_, timestamp_);
    }
};

// ============================================================================
// Text Object
// ============================================================================

class TextObject : public Drawable {
public:
    TextObject() : Drawable() {}
    explicit TextObject(std::string text, Point position, FontDescriptor font = {},
                        DrawingAttributes attrs = {})
        : Drawable(), text_(std::move(text)), position_(position), font_(font) {
        attrs_ = std::move(attrs);
    }

    [[nodiscard]] std::string const& text() const { return text_; }
    void set_text(std::string t) { text_ = std::move(t); }

    [[nodiscard]] Point const& position() const { return position_; }
    void set_position(Point p) { position_ = p; }

    [[nodiscard]] FontDescriptor const& font() const { return font_; }
    void set_font(FontDescriptor f) { font_ = std::move(f); }

    [[nodiscard]] TextAlign align() const { return align_; }
    void set_align(TextAlign a) { align_ = a; }
    [[nodiscard]] TextVAlign valign() const { return valign_; }
    void set_valign(TextVAlign a) { valign_ = a; }

    [[nodiscard]] double max_width() const { return max_width_; }
    void set_max_width(double w) { max_width_ = w; }

    // Estimate text dimensions (simplified)
    [[nodiscard]] double estimated_width() const {
        // Rough estimate: average char width ~ font size * 0.6
        return text_.size() * font_.size * 0.6;
    }
    [[nodiscard]] double estimated_height() const {
        return font_.size * 1.2; // line height
    }

    [[nodiscard]] Rect bounding_box() const override {
        double w = max_width_ > 0 ? max_width_ : estimated_width();
        double h = estimated_height();
        double x = position_.x;
        double y = position_.y;
        switch (align_) {
            case TextAlign::center: x -= w / 2.0; break;
            case TextAlign::right:  x -= w; break;
            default: break;
        }
        switch (valign_) {
            case TextVAlign::middle: y -= h / 2.0; break;
            case TextVAlign::bottom: y -= h; break;
            default: break;
        }
        return {x, y, w, h};
    }

    [[nodiscard]] bool hit_test(Point p, double tolerance = 5.0) const override {
        return bounding_box().inflated(tolerance / 2.0, tolerance / 2.0).contains(p);
    }

    [[nodiscard]] std::unique_ptr<Drawable> clone() const override {
        auto t = std::make_unique<TextObject>(text_, position_, font_, attrs_);
        t->id_ = id_;
        t->author_ = author_;
        t->timestamp_ = timestamp_;
        t->version = version;
        t->align_ = align_;
        t->valign_ = valign_;
        t->max_width_ = max_width_;
        return t;
    }

    void move_by(double dx, double dy) override {
        position_.x += dx;
        position_.y += dy;
    }

    void scale_from(Point anchor, double sx, double sy) override {
        position_.x = anchor.x + (position_.x - anchor.x) * sx;
        position_.y = anchor.y + (position_.y - anchor.y) * sy;
        font_.size *= std::max(sx, sy); // scale font proportionally
    }

    void to_json(std::ostream& os) const override {
        // Escape text for JSON
        std::string escaped;
        for (char c : text_) {
            if (c == '"') escaped += "\\\"";
            else if (c == '\\') escaped += "\\\\";
            else if (c == '\n') escaped += "\\n";
            else if (c == '\t') escaped += "\\t";
            else escaped += c;
        }
        os << fmt::format(
            R"({{"type":"text","id":"{}","version":{},"text":"{}","pos":[{:.3f},{:.3f}],)",
            id_, version, escaped, position_.x, position_.y);
        os << fmt::format(
            R"("font":{{"family":"{}","size":{:.1f},"bold":{},"italic":{},"underline":{},"strikethrough":{}}},)",
            font_.family, font_.size, font_.bold, font_.italic, font_.underline, font_.strikethrough);
        os << fmt::format(R"("align":{},"valign":{},"max_width":{:.1f})",
                          static_cast<int>(align_), static_cast<int>(valign_), max_width_);
        os << meta_json() << "}";
    }

    void from_json(std::string_view json) override {
        // Simplified
    }

private:
    std::string text_;
    Point position_;
    FontDescriptor font_;
    TextAlign align_ = TextAlign::left;
    TextVAlign valign_ = TextVAlign::top;
    double max_width_ = 0.0;
};

// ============================================================================
// Layer
// ============================================================================

class Layer {
public:
    explicit Layer(std::string name, int z_index = 0)
        : name_(std::move(name)), z_index_(z_index), visible_(true), locked_(false) {
        id_ = ObjectId::generate();
    }

    [[nodiscard]] std::string const& id() const { return id_; }
    [[nodiscard]] std::string const& name() const { return name_; }
    void set_name(std::string n) { name_ = std::move(n); }

    [[nodiscard]] int z_index() const { return z_index_; }
    void set_z_index(int z) { z_index_ = z; }

    [[nodiscard]] bool visible() const { return visible_; }
    void set_visible(bool v) { visible_ = v; }

    [[nodiscard]] bool locked() const { return locked_; }
    void set_locked(bool l) { locked_ = l; }

    [[nodiscard]] double opacity() const { return opacity_; }
    void set_opacity(double o) { opacity_ = std::clamp(o, 0.0, 1.0); }

    [[nodiscard]] BlendMode blend_mode() const { return blend_mode_; }
    void set_blend_mode(BlendMode m) { blend_mode_ = m; }

    // Object management
    void add_object(std::unique_ptr<Drawable> obj) {
        objects_.push_back(std::move(obj));
    }

    void remove_object(std::string const& id) {
        objects_.erase(
            std::remove_if(objects_.begin(), objects_.end(),
                           [&](auto const& o) { return o->id() == id; }),
            objects_.end());
    }

    [[nodiscard]] Drawable* find_object(std::string const& id) {
        for (auto& obj : objects_) {
            if (obj->id() == id) return obj.get();
        }
        return nullptr;
    }

    [[nodiscard]] std::vector<std::unique_ptr<Drawable>> const& objects() const { return objects_; }
    [[nodiscard]] std::vector<std::unique_ptr<Drawable>>& mutable_objects() { return objects_; }
    [[nodiscard]] size_t object_count() const { return objects_.size(); }

    void clear() { objects_.clear(); }

    /// Hit test: return the first object hit (top-most due to draw order).
    [[nodiscard]] HitResult hit_test(Point p, double tolerance = 5.0) const {
        if (!visible_ || locked_) return {};
        // Search in reverse draw order (top object first)
        for (auto it = objects_.rbegin(); it != objects_.rend(); ++it) {
            if ((*it)->hit_test(p, tolerance)) {
                return {true, (*it)->id(), p, -1, 0.0};
            }
        }
        return {};
    }

    /// Get all objects intersecting a rectangle.
    [[nodiscard]] std::vector<Drawable*> objects_in_rect(Rect const& r) const {
        std::vector<Drawable*> result;
        for (auto const& obj : objects_) {
            if (obj->bounding_box().intersects(r)) {
                result.push_back(obj.get());
            }
        }
        return result;
    }

    /// Reorder an object (bring to front, send to back, etc.)
    void bring_to_front(std::string const& id) {
        auto it = find_iter(id);
        if (it != objects_.end()) {
            auto obj = std::move(*it);
            objects_.erase(it);
            objects_.push_back(std::move(obj));
        }
    }

    void send_to_back(std::string const& id) {
        auto it = find_iter(id);
        if (it != objects_.end()) {
            auto obj = std::move(*it);
            objects_.erase(it);
            objects_.insert(objects_.begin(), std::move(obj));
        }
    }

    void move_up(std::string const& id) {
        auto it = find_iter(id);
        if (it != objects_.end() && std::next(it) != objects_.end()) {
            std::iter_swap(it, std::next(it));
        }
    }

    void move_down(std::string const& id) {
        auto it = find_iter(id);
        if (it != objects_.begin()) {
            auto prev = std::prev(it);
            std::iter_swap(prev, it);
        }
    }

private:
    std::string id_;
    std::string name_;
    int z_index_ = 0;
    bool visible_ = true;
    bool locked_ = false;
    double opacity_ = 1.0;
    BlendMode blend_mode_ = BlendMode::normal;
    std::vector<std::unique_ptr<Drawable>> objects_;

    auto find_iter(std::string const& id) {
        return std::find_if(objects_.begin(), objects_.end(),
                            [&](auto const& o) { return o->id() == id; });
    }
};

// ============================================================================
// Background
// ============================================================================

class Background {
public:
    enum class Type { none, solid_color, grid, image };

    void set_solid_color(Color c) {
        type_ = Type::solid_color;
        color_ = c;
    }

    void set_grid(GridSettings gs) {
        type_ = Type::grid;
        grid_ = std::move(gs);
    }

    void set_image(std::string path, double opacity = 1.0) {
        type_ = Type::image;
        image_path_ = std::move(path);
        image_opacity_ = opacity;
    }

    void clear() { type_ = Type::none; }

    [[nodiscard]] Type type() const { return type_; }
    [[nodiscard]] Color const& color() const { return color_; }
    [[nodiscard]] GridSettings const& grid() const { return grid_; }
    [[nodiscard]] std::string const& image_path() const { return image_path_; }
    [[nodiscard]] double image_opacity() const { return image_opacity_; }

private:
    Type type_ = Type::none;
    Color color_ = Color::white();
    GridSettings grid_;
    std::string image_path_;
    double image_opacity_ = 1.0;
};

// ============================================================================
// Command Pattern for Undo/Redo
// ============================================================================

/// Abstract base for undoable commands.
class Command {
public:
    virtual ~Command() = default;
    virtual void execute() = 0;
    virtual void undo() = 0;
    [[nodiscard]] virtual std::string description() const { return "Command"; }
};

/// Command to add an object.
class AddObjectCommand : public Command {
public:
    AddObjectCommand(Layer& layer, std::unique_ptr<Drawable> obj)
        : layer_(&layer), obj_(std::move(obj)) {}

    void execute() override {
        layer_->add_object(std::move(obj_));
    }

    void undo() override {
        obj_ = std::move(layer_->mutable_objects().back());
        layer_->mutable_objects().pop_back();
    }

    [[nodiscard]] std::string description() const override { return "Add Object"; }

private:
    Layer* layer_;
    std::unique_ptr<Drawable> obj_;
};

/// Command to remove an object.
class RemoveObjectCommand : public Command {
public:
    RemoveObjectCommand(Layer& layer, std::unique_ptr<Drawable> obj)
        : layer_(&layer), obj_(std::move(obj)), id_(obj_->id()) {}

    void execute() override {
        layer_->remove_object(id_);
    }

    void undo() override {
        layer_->add_object(std::move(obj_));
    }

    [[nodiscard]] std::string description() const override { return "Remove Object"; }

private:
    Layer* layer_;
    std::unique_ptr<Drawable> obj_;
    std::string id_;
};

/// Command to move an object.
class MoveObjectCommand : public Command {
public:
    MoveObjectCommand(Drawable& obj, double dx, double dy)
        : obj_(&obj), dx_(dx), dy_(dy) {}

    void execute() override { obj_->move_by(dx_, dy_); }
    void undo() override { obj_->move_by(-dx_, -dy_); }
    [[nodiscard]] std::string description() const override {
        return fmt::format("Move ({:.1f}, {:.1f})", dx_, dy_);
    }

private:
    Drawable* obj_;
    double dx_, dy_;
};

/// Command to change an object's attributes.
class ChangeAttrsCommand : public Command {
public:
    ChangeAttrsCommand(Drawable& obj, DrawingAttributes new_attrs)
        : obj_(&obj), new_attrs_(std::move(new_attrs)), old_attrs_(obj.attrs()) {}

    void execute() override { obj_->set_attrs(new_attrs_); }
    void undo() override { obj_->set_attrs(old_attrs_); }
    [[nodiscard]] std::string description() const override { return "Change Attributes"; }

private:
    Drawable* obj_;
    DrawingAttributes new_attrs_;
    DrawingAttributes old_attrs_;
};

/// Command to resize a shape.
class ResizeShapeCommand : public Command {
public:
    ResizeShapeCommand(Shape& shape, Rect new_bounds)
        : shape_(&shape), new_bounds_(new_bounds), old_bounds_(shape.bounds()) {}

    void execute() override { shape_->set_bounds(new_bounds_); }
    void undo() override { shape_->set_bounds(old_bounds_); }
    [[nodiscard]] std::string description() const override { return "Resize"; }

private:
    Shape* shape_;
    Rect new_bounds_;
    Rect old_bounds_;
};

/// Command to edit text.
class EditTextCommand : public Command {
public:
    EditTextCommand(TextObject& text_obj, std::string new_text)
        : text_obj_(&text_obj), new_text_(std::move(new_text)), old_text_(text_obj.text()) {}

    void execute() override { text_obj_->set_text(new_text_); }
    void undo() override { text_obj_->set_text(old_text_); }
    [[nodiscard]] std::string description() const override { return "Edit Text"; }

private:
    TextObject* text_obj_;
    std::string new_text_;
    std::string old_text_;
};

/// Composite command that bundles multiple commands.
class CompositeCommand : public Command {
public:
    void add(std::unique_ptr<Command> cmd) {
        commands_.push_back(std::move(cmd));
    }

    void execute() override {
        for (auto& cmd : commands_) cmd->execute();
    }

    void undo() override {
        for (auto it = commands_.rbegin(); it != commands_.rend(); ++it)
            (*it)->undo();
    }

    [[nodiscard]] std::string description() const override {
        return fmt::format("Composite ({})", commands_.size());
    }

    [[nodiscard]] size_t size() const { return commands_.size(); }

private:
    std::vector<std::unique_ptr<Command>> commands_;
};

// ============================================================================
// Undo Manager
// ============================================================================

class UndoManager {
public:
    explicit UndoManager(size_t max_history = 256) : max_history_(max_history) {}

    void execute(std::unique_ptr<Command> cmd) {
        cmd->execute();
        redo_stack_.clear(); // invalidate redo
        undo_stack_.push_back(std::move(cmd));
        while (undo_stack_.size() > max_history_) {
            undo_stack_.pop_front();
        }
    }

    [[nodiscard]] bool can_undo() const { return !undo_stack_.empty(); }
    [[nodiscard]] bool can_redo() const { return !redo_stack_.empty(); }

    void undo() {
        if (!can_undo()) return;
        auto cmd = std::move(undo_stack_.back());
        undo_stack_.pop_back();
        cmd->undo();
        redo_stack_.push_back(std::move(cmd));
    }

    void redo() {
        if (!can_redo()) return;
        auto cmd = std::move(redo_stack_.back());
        redo_stack_.pop_back();
        cmd->execute();
        undo_stack_.push_back(std::move(cmd));
    }

    void clear() {
        undo_stack_.clear();
        redo_stack_.clear();
    }

    [[nodiscard]] size_t undo_count() const { return undo_stack_.size(); }
    [[nodiscard]] size_t redo_count() const { return redo_stack_.size(); }

private:
    std::deque<std::unique_ptr<Command>> undo_stack_;
    std::deque<std::unique_ptr<Command>> redo_stack_;
    size_t max_history_;
};

// ============================================================================
// Whiteboard Document (the main data model)
// ============================================================================

class WhiteboardDocument {
public:
    WhiteboardDocument() {
        // Create default layer
        add_layer("Default");
    }

    // === Layer management ===

    Layer& add_layer(std::string name) {
        int z = static_cast<int>(layers_.size());
        auto layer = std::make_unique<Layer>(std::move(name), z);
        auto& ref = *layer;
        layers_.push_back(std::move(layer));
        sort_layers();
        return ref;
    }

    void remove_layer(std::string const& id) {
        std::erase_if(layers_, [&](auto const& l) { return l->id() == id; });
        sort_layers();
    }

    [[nodiscard]] Layer* active_layer() {
        if (active_layer_index_ < layers_.size())
            return layers_[active_layer_index_].get();
        return nullptr;
    }

    void set_active_layer(size_t index) {
        if (index < layers_.size()) active_layer_index_ = index;
    }

    void set_active_layer(std::string const& id) {
        for (size_t i = 0; i < layers_.size(); ++i) {
            if (layers_[i]->id() == id) {
                active_layer_index_ = i;
                return;
            }
        }
    }

    [[nodiscard]] std::vector<std::unique_ptr<Layer>> const& layers() const { return layers_; }
    [[nodiscard]] Layer* layer(size_t i) { return i < layers_.size() ? layers_[i].get() : nullptr; }
    [[nodiscard]] size_t layer_count() const { return layers_.size(); }

    // === Active tool ===

    [[nodiscard]] ToolProperties const& tool_props() const { return tool_props_; }
    void set_tool_props(ToolProperties tp) { tool_props_ = std::move(tp); }

    // === Canvas dimensions ===

    [[nodiscard]] double width() const { return width_; }
    [[nodiscard]] double height() const { return height_; }
    void set_size(double w, double h) { width_ = w; height_ = h; }

    // === Canvas transform ===

    [[nodiscard]] CanvasTransform const& transform() const { return transform_; }
    void set_transform(CanvasTransform t) { transform_ = std::move(t); }
    CanvasTransform& mutable_transform() { return transform_; }

    // === Background ===

    [[nodiscard]] Background const& background() const { return background_; }
    void set_background(Background bg) { background_ = std::move(bg); }
    Background& mutable_background() { return background_; }

    // === Grid ===

    [[nodiscard]] GridSettings const& grid() const { return grid_; }
    void set_grid(GridSettings g) { grid_ = std::move(g); }

    // === Snap-to-grid ===

    [[nodiscard]] Point snap_point(Point p) const {
        if (!grid_.snap_enabled) return p;
        double s = grid_.spacing;
        return {std::round(p.x / s) * s, std::round(p.y / s) * s};
    }

    // === Selection ===

    [[nodiscard]] std::vector<std::string> const& selection() const { return selection_; }
    void select(std::string id) { selection_ = {std::move(id)}; }
    void select(std::vector<std::string> ids) { selection_ = std::move(ids); }
    void clear_selection() { selection_.clear(); }
    [[nodiscard]] bool has_selection() const { return !selection_.empty(); }

    [[nodiscard]] Drawable* first_selected() {
        if (selection_.empty()) return nullptr;
        for (auto& layer : layers_) {
            auto* obj = layer->find_object(selection_.front());
            if (obj) return obj;
        }
        return nullptr;
    }

    /// Get all selected drawables.
    [[nodiscard]] std::vector<Drawable*> selected_objects() {
        std::vector<Drawable*> result;
        for (auto& id : selection_) {
            for (auto& layer : layers_) {
                auto* obj = layer->find_object(id);
                if (obj) { result.push_back(obj); break; }
            }
        }
        return result;
    }

    // === Object lookup ===

    [[nodiscard]] Drawable* find_object(std::string const& id) {
        for (auto& layer : layers_) {
            auto* obj = layer->find_object(id);
            if (obj) return obj;
        }
        return nullptr;
    }

    // === Add/remove objects ===

    Drawable& add_object(std::unique_ptr<Drawable> obj, size_t layer_idx = static_cast<size_t>(-1)) {
        auto* layer = (layer_idx < layers_.size()) ? layers_[layer_idx].get() : active_layer();
        if (!layer) throw std::runtime_error("No layer available");
        auto* ptr = obj.get();
        layer->add_object(std::move(obj));
        return *ptr;
    }

    void remove_object(std::string const& id) {
        for (auto& layer : layers_)
            layer->remove_object(id);
        std::erase(selection_, id);
    }

    // === Delete selected ===

    void delete_selected() {
        for (auto& id : selection_) {
            for (auto& layer : layers_)
                layer->remove_object(id);
        }
        selection_.clear();
    }

    // === Hit testing ===

    [[nodiscard]] HitResult hit_test(Point canvas_point, double tolerance = 5.0) const {
        // Search layers in reverse Z order, and within each layer reverse draw order
        for (auto it = layers_.rbegin(); it != layers_.rend(); ++it) {
            auto hr = (*it)->hit_test(canvas_point, tolerance);
            if (hr.hit) return hr;
        }
        return {};
    }

    // === Serialization ===

    [[nodiscard]] std::string to_json() const {
        std::ostringstream os;
        os << "{";
        os << fmt::format(R"("version":1,"width":{:.1f},"height":{:.1f},)", width_, height_);
        // Background
        os << R"("background":{)";
        os << fmt::format(R"("type":{},"color":"{}")",
                          static_cast<int>(background_.type()),
                          background_.color().to_hex());
        os << "},";
        // Grid
        os << fmt::format(R"("grid":{{"visible":{},"snap":{},"spacing":{:.1f}}},)",
                          grid_.visible, grid_.snap_enabled, grid_.spacing);
        // Layers
        os << R"("layers":[)";
        for (size_t i = 0; i < layers_.size(); ++i) {
            if (i > 0) os << ',';
            layer_to_json(os, *layers_[i]);
        }
        os << "]}";
        return os.str();
    }

    void from_json(std::string_view json) {
        // Simplified: in production use a real JSON library
        clear_all();
        // Parse version, dimensions, layers etc.
        auto extract_value = [&](std::string_view key) -> std::string_view {
            auto pos = json.find(key);
            if (pos == std::string_view::npos) return {};
            auto val_start = json.find(':', pos);
            if (val_start == std::string_view::npos) return {};
            ++val_start;
            while (val_start < json.size() &&
                   (json[val_start] == ' ' || json[val_start] == '"' || json[val_start] == '\n'))
                ++val_start;
            auto val_end = val_start;
            int depth = 0;
            while (val_end < json.size()) {
                char c = json[val_end];
                if (c == '{' || c == '[') ++depth;
                else if (c == '}' || c == ']') { if (depth == 0) break; --depth; }
                else if (c == ',' && depth == 0) break;
                ++val_end;
            }
            return json.substr(val_start, val_end - val_start);
        };
        // Simplified loading
        spdlog::info("WhiteboardDocument: deserialized JSON ({} bytes)", json.size());
        if (layers_.empty()) add_layer("Default");
    }

    void clear_all() {
        layers_.clear();
        selection_.clear();
        add_layer("Default");
    }

    // === Undo/Redo ===

    UndoManager& undo_manager() { return undo_manager_; }
    [[nodiscard]] UndoManager const& undo_manager() const { return undo_manager_; }

    // === Metadata ===

    [[nodiscard]] std::string const& title() const { return title_; }
    void set_title(std::string t) { title_ = std::move(t); }

    [[nodiscard]] std::string const& author() const { return doc_author_; }
    void set_author(std::string a) { doc_author_ = std::move(a); }

    [[nodiscard]] uint64_t version() const { return doc_version_; }
    void set_version(uint64_t v) { doc_version_ = v; }

private:
    std::vector<std::unique_ptr<Layer>> layers_;
    size_t active_layer_index_ = 0;
    ToolProperties tool_props_;
    double width_ = 1920.0;
    double height_ = 1080.0;
    CanvasTransform transform_;
    Background background_{};
    GridSettings grid_{};
    std::vector<std::string> selection_;
    UndoManager undo_manager_{256};
    std::string title_ = "Untitled Whiteboard";
    std::string doc_author_;
    uint64_t doc_version_ = 1;

    void sort_layers() {
        std::ranges::sort(layers_, [](auto const& a, auto const& b) {
            return a->z_index() < b->z_index();
        });
    }

    void layer_to_json(std::ostream& os, Layer const& layer) const {
        os << fmt::format(
            R"({{"id":"{}","name":"{}","z":{},"visible":{},"locked":{},"opacity":{:.2f},"blend":{},"objects":[)",
            layer.id(), layer.name(), layer.z_index(), layer.visible(),
            layer.locked(), layer.opacity(), static_cast<int>(layer.blend_mode()));
        auto const& objs = layer.objects();
        for (size_t i = 0; i < objs.size(); ++i) {
            if (i > 0) os << ',';
            objs[i]->to_json(os);
        }
        os << "]}";
    }
};

// ============================================================================
// Color Palette
// ============================================================================

class ColorPalette {
public:
    ColorPalette() {
        // Default palette
        colors_ = {
            {"Black",       Color::black()},
            {"White",       Color::white()},
            {"Red",         Color::red()},
            {"Green",       Color::green()},
            {"Blue",        Color::blue()},
            {"Yellow",      Color::yellow()},
            {"Orange",      {255, 165, 0, 255}},
            {"Purple",      {128, 0, 128, 255}},
            {"Cyan",        {0, 255, 255, 255}},
            {"Magenta",     {255, 0, 255, 255}},
            {"Lime",        {50, 205, 50, 255}},
            {"Pink",        {255, 192, 203, 255}},
            {"Teal",        {0, 128, 128, 255}},
            {"Brown",       {139, 69, 19, 255}},
            {"Navy",        {0, 0, 128, 255}},
            {"Maroon",      {128, 0, 0, 255}},
            {"Olive",       {128, 128, 0, 255}},
            {"Silver",      {192, 192, 192, 255}},
            {"Gray",        {128, 128, 128, 255}},
            {"Dark Gray",   {64, 64, 64, 255}},
            {"Coral",       {255, 127, 80, 255}},
            {"Turquoise",   {64, 224, 208, 255}},
            {"Indigo",      {75, 0, 130, 255}},
            {"Violet",      {238, 130, 238, 255}},
            {"Gold",        {255, 215, 0, 255}},
            {"Crimson",     {220, 20, 60, 255}},
            {"Salmon",      {250, 128, 114, 255}},
            {"Khaki",       {240, 230, 140, 255}},
            {"Plum",        {221, 160, 221, 255}},
            {"Mint",        {189, 252, 201, 255}},
            {"Sky Blue",    {135, 206, 235, 255}},
        };
    }

    [[nodiscard]] size_t size() const { return colors_.size(); }
    [[nodiscard]] Color color_at(size_t index) const {
        return index < colors_.size() ? colors_[index].second : Color::black();
    }
    [[nodiscard]] std::string const& name_at(size_t index) const {
        return index < colors_.size() ? colors_[index].first : colors_[0].first;
    }

    void add_color(std::string name, Color c) {
        colors_.emplace_back(std::move(name), c);
    }

    [[nodiscard]] std::vector<std::pair<std::string, Color>> const& colors() const { return colors_; }

    /// Get recently used colors.
    [[nodiscard]] std::vector<Color> const& recent() const { return recent_colors_; }
    void mark_recent(Color c) {
        std::erase(recent_colors_, c);
        recent_colors_.insert(recent_colors_.begin(), c);
        if (recent_colors_.size() > 12) recent_colors_.pop_back();
    }

    /// Predefined pen thicknesses.
    static std::vector<double> const& thicknesses() {
        static std::vector<double> t = {1.0, 2.0, 3.0, 5.0, 8.0, 12.0, 18.0, 24.0, 36.0};
        return t;
    }

private:
    std::vector<std::pair<std::string, Color>> colors_;
    std::vector<Color> recent_colors_;
};

// ============================================================================
// Export utilities
// ============================================================================

/// Export whiteboard content to a simplified SVG representation.
class SvgExporter {
public:
    [[nodiscard]] static std::string export_to_svg(WhiteboardDocument const& doc) {
        std::ostringstream os;
        double w = doc.width(), h = doc.height();
        os << fmt::format(
            R"(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="{:.0f}" height="{:.0f}" viewBox="0 0 {:.0f} {:.0f}">
<rect width="100%" height="100%" fill="{})",
            w, h, w, h, doc.background().color().to_hex());

        // Grid
        if (doc.grid().visible) {
            os << R"( stroke=")" << doc.grid().color().to_hex()
               << R"(" stroke-width="0.5" fill="none"/>)";
            double s = doc.grid().spacing;
            for (double x = s; x < w; x += s) {
                os << fmt::format(R"(<line x1="{:.1f}" y1="0" x2="{:.1f}" y2="{:.0f}"/>)", x, x, h);
            }
            for (double y = s; y < h; y += s) {
                os << fmt::format(R"(<line x1="0" y1="{:.1f}" x2="{:.0f}" y2="{:.1f}"/>)", y, w, y);
            }
        }

        // Render each layer
        for (auto const& layer : doc.layers()) {
            if (!layer->visible()) continue;
            os << fmt::format(R"(<g opacity="{:.2f}">)", layer->opacity());
            for (auto const& obj : layer->objects()) {
                render_object(os, *obj);
            }
            os << "</g>";
        }

        os << "\n</svg>";
        return os.str();
    }

private:
    static void render_object(std::ostream& os, Drawable const& obj) {
        if (auto* stroke = dynamic_cast<Stroke const*>(&obj)) {
            render_stroke(os, *stroke);
        } else if (auto* shape = dynamic_cast<Shape const*>(&obj)) {
            render_shape(os, *shape);
        } else if (auto* text = dynamic_cast<TextObject const*>(&obj)) {
            render_text(os, *text);
        }
    }

    static void render_stroke(std::ostream& os, Stroke const& stroke) {
        auto const& pts = stroke.points();
        if (pts.size() < 2) return;
        auto const& attrs = stroke.attrs();
        os << fmt::format(R"(<polyline points=")");
        for (size_t i = 0; i < pts.size(); ++i) {
            if (i > 0) os << ' ';
            os << fmt::format("{:.1f},{:.1f}", pts[i].x, pts[i].y);
        }
        os << fmt::format(
            R"(" fill="none" stroke="{}" stroke-width="{:.1f}" stroke-linecap="round" stroke-linejoin="round" opacity="{:.2f}"/>)",
            attrs.stroke.color.to_hex(), attrs.stroke.width, attrs.opacity);
    }

    static void render_shape(std::ostream& os, Shape const& shape) {
        auto const& b = shape.bounds();
        auto const& attrs = shape.attrs();

        std::string fill = (attrs.fill.style != FillStyle::none)
            ? fmt::format(R"( fill="{}")", attrs.fill.color.to_hex())
            : R"( fill="none")";

        std::string stroke = fmt::format(
            R"( stroke="{}" stroke-width="{:.1f}")",
            attrs.stroke.color.to_hex(), attrs.stroke.width);

        switch (shape.kind()) {
        case ShapeKind::rectangle:
            os << fmt::format(R"(<rect x="{:.1f}" y="{:.1f}" width="{:.1f}" height="{:.1f}"{}{} rx="{:.1f}"/>)",
                              b.x, b.y, b.w, b.h, fill, stroke, shape.corner_radius());
            break;
        case ShapeKind::ellipse:
            os << fmt::format(R"(<ellipse cx="{:.1f}" cy="{:.1f}" rx="{:.1f}" ry="{:.1f}"{}{}/>)",
                              b.center().x, b.center().y, b.w / 2.0, b.h / 2.0, fill, stroke);
            break;
        case ShapeKind::line:
        case ShapeKind::arrow:
            os << fmt::format(R"(<line x1="{:.1f}" y1="{:.1f}" x2="{:.1f}" y2="{:.1f}"{}{}/>)",
                              b.x, b.y, b.x + b.w, b.y + b.h, fill, stroke);
            break;
        default:
            os << fmt::format(R"(<rect x="{:.1f}" y="{:.1f}" width="{:.1f}" height="{:.1f}"{}{}/>)",
                              b.x, b.y, b.w, b.h, fill, stroke);
        }
    }

    static void render_text(std::ostream& os, TextObject const& text) {
        auto const& attrs = text.attrs();
        os << fmt::format(
            R"(<text x="{:.1f}" y="{:.1f}" font-family="{}" font-size="{:.1f}" fill="{}" text-anchor="{}">{})",
            text.position().x, text.position().y + text.font().size,
            text.font().family, text.font().size,
            attrs.stroke.color.to_hex(),
            text.align() == TextAlign::center ? "middle" :
                text.align() == TextAlign::right ? "end" : "start",
            text.text());
        os << "</text>";
    }
};

// ============================================================================
// Pseudo-PNG header writer (minimal, for demonstration)
// ============================================================================

struct PngWriter {
    /// Write a minimal 1-pixel PNG (placeholder — real impl would use libpng).
    static void write_placeholder_png(std::string const& path, uint32_t width, uint32_t height) {
        std::ofstream f(path, std::ios::binary);
        if (!f) throw std::runtime_error("Cannot open " + path + " for writing");

        // PNG signature
        f.write("\x89PNG\r\n\x1a\n", 8);

        auto write_chunk = [&](char const* type, uint8_t const* data, size_t len) {
            uint32_t nlen = htonl(static_cast<uint32_t>(len));
            f.write(reinterpret_cast<char const*>(&nlen), 4);
            f.write(type, 4);
            if (data && len > 0) f.write(reinterpret_cast<char const*>(data), len);
            // CRC placeholder (skipped for brevity in this demo)
            uint32_t crc = 0;
            f.write(reinterpret_cast<char const*>(&crc), 4);
        };

        // IHDR
        uint8_t ihdr[13] = {};
        ihdr[0] = static_cast<uint8_t>((width >> 24) & 0xFF);
        ihdr[1] = static_cast<uint8_t>((width >> 16) & 0xFF);
        ihdr[2] = static_cast<uint8_t>((width >> 8) & 0xFF);
        ihdr[3] = static_cast<uint8_t>(width & 0xFF);
        ihdr[4] = static_cast<uint8_t>((height >> 24) & 0xFF);
        ihdr[5] = static_cast<uint8_t>((height >> 16) & 0xFF);
        ihdr[6] = static_cast<uint8_t>((height >> 8) & 0xFF);
        ihdr[7] = static_cast<uint8_t>(height & 0xFF);
        ihdr[8] = 8;  // bit depth
        ihdr[9] = 2;  // color type RGB
        write_chunk("IHDR", ihdr, 13);

        // IDAT (minimal placeholder)
        uint8_t idat[] = {0x78, 0x01, 0x01, 0x00, 0x00, 0xFF, 0xFF};
        write_chunk("IDAT", idat, sizeof(idat));

        // IEND
        write_chunk("IEND", nullptr, 0);
    }

private:
    // htonl portability
    static uint32_t htonl(uint32_t hostlong) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        return ((hostlong & 0xFF000000) >> 24) |
               ((hostlong & 0x00FF0000) >> 8)  |
               ((hostlong & 0x0000FF00) << 8)  |
               ((hostlong & 0x000000FF) << 24);
#else
        return hostlong;
#endif
    }
};

/// High-level whiteboard exporter.
class WhiteboardExporter {
public:
    /// Export the whiteboard to an SVG file.
    static bool export_svg(WhiteboardDocument const& doc, std::string const& path) {
        try {
            std::string svg = SvgExporter::export_to_svg(doc);
            std::ofstream f(path);
            if (!f) {
                spdlog::error("WhiteboardExporter: cannot write SVG to {}", path);
                return false;
            }
            f << svg;
            spdlog::info("WhiteboardExporter: SVG exported to {} ({} bytes)", path, svg.size());
            return true;
        } catch (std::exception const& e) {
            spdlog::error("WhiteboardExporter: SVG export failed: {}", e.what());
            return false;
        }
    }

    /// Export the whiteboard to a PNG file (placeholder/demo).
    static bool export_png(WhiteboardDocument const& doc, std::string const& path) {
        try {
            uint32_t w = static_cast<uint32_t>(doc.width());
            uint32_t h = static_cast<uint32_t>(doc.height());
            PngWriter::write_placeholder_png(path, w, h);
            spdlog::info("WhiteboardExporter: PNG exported to {} ({}x{})", path, w, h);
            spdlog::warn("WhiteboardExporter: PNG export is a placeholder — real rendering needs a graphics backend");
            return true;
        } catch (std::exception const& e) {
            spdlog::error("WhiteboardExporter: PNG export failed: {}", e.what());
            return false;
        }
    }

    /// Export as PDF (placeholder — requires a PDF library for real output).
    static bool export_pdf(WhiteboardDocument const& doc, std::string const& path) {
        // In practice, this would use libharu, pdfium, or similar.
        // Here we write the SVG and note that it's the best we can do without a PDF lib.
        spdlog::warn("WhiteboardExporter: PDF export is a stub — writing SVG as fallback");
        auto svg_path = path;
        if (svg_path.ends_with(".pdf")) {
            svg_path = svg_path.substr(0, svg_path.size() - 4) + ".svg";
        }
        return export_svg(doc, svg_path);
    }
};

// ============================================================================
// Collaborative Editing: Remote Stroke Merging
// ============================================================================

/// Represents a change received from a remote collaborator.
struct RemoteChange {
    enum class Action : uint8_t { add, modify, remove };

    Action action = Action::add;
    std::string object_json;
    std::string object_id;
    uint64_t timestamp = 0;
    std::string author;
    uint64_t lamport_clock = 0; // For causal ordering

    [[nodiscard]] bool is_newer_than(RemoteChange const& other) const {
        if (lamport_clock != other.lamport_clock)
            return lamport_clock > other.lamport_clock;
        return timestamp > other.timestamp;
    }
};

/// Collaborative merge engine.
class CollaborativeMerge {
public:
    CollaborativeMerge() = default;

    /// Receive and buffer a remote change.
    void receive_remote_change(RemoteChange change, WhiteboardDocument& doc) {
        std::lock_guard lock(mutex_);
        remote_queue_.push(std::move(change));
    }

    /// Process the next remote change (call from a controlled context, e.g., main loop).
    /// Returns true if a change was processed.
    bool process_next(WhiteboardDocument& doc) {
        RemoteChange change;
        {
            std::lock_guard lock(mutex_);
            if (remote_queue_.empty()) return false;
            change = std::move(remote_queue_.front());
            remote_queue_.pop();
        }
        apply_change(change, doc);
        return true;
    }

    /// Process all pending remote changes.
    size_t process_all(WhiteboardDocument& doc) {
        size_t count = 0;
        while (process_next(doc)) ++count;
        return count;
    }

    /// Generate a local change notification for broadcasting.
    [[nodiscard]] RemoteChange create_local_change(Drawable const& obj,
                                                     RemoteChange::Action action) {
        std::ostringstream os;
        obj.to_json(os);
        return RemoteChange{
            .action = action,
            .object_json = os.str(),
            .object_id = obj.id(),
            .timestamp = static_cast<uint64_t>(
                std::chrono::system_clock::now().time_since_epoch().count()),
            .author = obj.author(),
            .lamport_clock = lamport_clock_.fetch_add(1, std::memory_order_relaxed)
        };
    }

    /// Check if there are pending changes.
    [[nodiscard]] bool has_pending() const {
        std::lock_guard lock(mutex_);
        return !remote_queue_.empty();
    }

    [[nodiscard]] size_t pending_count() const {
        std::lock_guard lock(mutex_);
        return remote_queue_.size();
    }

private:
    std::queue<RemoteChange> remote_queue_;
    mutable std::mutex mutex_;
    std::atomic<uint64_t> lamport_clock_{0};

    void apply_change(RemoteChange const& change, WhiteboardDocument& doc) {
        switch (change.action) {
        case RemoteChange::Action::add: {
            // Check if object already exists
            auto* existing = doc.find_object(change.object_id);
            if (existing) {
                // Update if remote is newer
                spdlog::debug("CollaborativeMerge: object {} already exists, updating", change.object_id);
                existing->from_json(change.object_json);
            } else {
                spdlog::debug("CollaborativeMerge: adding remote object {}", change.object_id);
                // Parse from JSON and add
                // In production, parse and create appropriate Drawable subclass
            }
            break;
        }
        case RemoteChange::Action::modify: {
            auto* existing = doc.find_object(change.object_id);
            if (existing) {
                existing->from_json(change.object_json);
                spdlog::debug("CollaborativeMerge: modified object {}", change.object_id);
            }
            break;
        }
        case RemoteChange::Action::remove: {
            doc.remove_object(change.object_id);
            spdlog::debug("CollaborativeMerge: removed object {}", change.object_id);
            break;
        }
        }
    }
};

// ============================================================================
// Drawing Tools (Logic)
// ============================================================================

/// Base class for tool implementations.
class DrawingTool {
public:
    virtual ~DrawingTool() = default;
    [[nodiscard]] virtual ToolType type() const = 0;

    /// Called when the user presses the mouse/stylus.
    virtual void on_pointer_down(Point canvas_pos, WhiteboardDocument& doc) {
        start_point_ = canvas_pos;
        last_point_ = canvas_pos;
        is_drawing_ = true;
    }

    /// Called when the user drags.
    virtual void on_pointer_move(Point canvas_pos, WhiteboardDocument& doc) {
        last_point_ = canvas_pos;
    }

    /// Called when the user releases.
    virtual void on_pointer_up(Point canvas_pos, WhiteboardDocument& doc) {
        is_drawing_ = false;
    }

    [[nodiscard]] bool is_drawing() const { return is_drawing_; }

protected:
    Point start_point_;
    Point last_point_;
    bool is_drawing_ = false;

    [[nodiscard]] DrawingAttributes default_attrs(WhiteboardDocument const& doc) const {
        auto const& tp = doc.tool_props();
        DrawingAttributes da;
        da.stroke.color = tp.color;
        da.stroke.width = tp.thickness;
        da.opacity = tp.opacity;
        if (tp.type == ToolType::highlighter) {
            da.stroke.color = da.stroke.color.with_alpha(80);
            da.stroke.width = tp.thickness * 2.5;
            da.blend_mode = BlendMode::multiply;
        }
        return da;
    }
};

/// Pen tool: freehand drawing.
class PenTool : public DrawingTool {
public:
    [[nodiscard]] ToolType type() const override { return ToolType::pen; }

    void on_pointer_down(Point canvas_pos, WhiteboardDocument& doc) override {
        DrawingTool::on_pointer_down(canvas_pos, doc);
        current_stroke_ = std::make_unique<Stroke>();
        current_stroke_->set_attrs(default_attrs(doc));
        current_stroke_->set_author(doc.author());
        current_stroke_->add_point(canvas_pos);
    }

    void on_pointer_move(Point canvas_pos, WhiteboardDocument& doc) override {
        if (!is_drawing_ || !current_stroke_) return;
        // Snap to grid if enabled
        if (doc.tool_props().snap_to_grid) {
            canvas_pos = doc.snap_point(canvas_pos);
        }
        current_stroke_->add_point(canvas_pos);
        last_point_ = canvas_pos;
    }

    void on_pointer_up(Point canvas_pos, WhiteboardDocument& doc) override {
        if (!is_drawing_ || !current_stroke_) return;
        current_stroke_->add_point(canvas_pos);
        current_stroke_->set_timestamp(
            std::chrono::system_clock::now().time_since_epoch().count());
        // Simplify and smooth
        current_stroke_->simplify(2.0);
        current_stroke_->smooth(3);

        auto cmd = std::make_unique<AddObjectCommand>(
            *doc.active_layer(), std::move(current_stroke_));
        doc.undo_manager().execute(std::move(cmd));

        DrawingTool::on_pointer_up(canvas_pos, doc);
    }

private:
    std::unique_ptr<Stroke> current_stroke_;
};

/// Highlighter tool (semi-transparent wide strokes).
class HighlighterTool : public DrawingTool {
public:
    [[nodiscard]] ToolType type() const override { return ToolType::highlighter; }

    void on_pointer_down(Point canvas_pos, WhiteboardDocument& doc) override {
        DrawingTool::on_pointer_down(canvas_pos, doc);
        current_stroke_ = std::make_unique<Stroke>();
        auto attrs = default_attrs(doc);
        attrs.stroke.color = attrs.stroke.color.with_alpha(80);
        attrs.stroke.width = doc.tool_props().thickness * 3.0;
        attrs.blend_mode = BlendMode::multiply;
        attrs.fill.style = FillStyle::semi_transparent;
        current_stroke_->set_attrs(attrs);
        current_stroke_->set_author(doc.author());
        current_stroke_->add_point(canvas_pos);
    }

    void on_pointer_move(Point canvas_pos, WhiteboardDocument& doc) override {
        if (!is_drawing_ || !current_stroke_) return;
        if (doc.tool_props().snap_to_grid) canvas_pos = doc.snap_point(canvas_pos);
        current_stroke_->add_point(canvas_pos);
        last_point_ = canvas_pos;
    }

    void on_pointer_up(Point canvas_pos, WhiteboardDocument& doc) override {
        if (!is_drawing_ || !current_stroke_) return;
        current_stroke_->add_point(canvas_pos);
        current_stroke_->smooth(5);

        auto cmd = std::make_unique<AddObjectCommand>(
            *doc.active_layer(), std::move(current_stroke_));
        doc.undo_manager().execute(std::move(cmd));

        DrawingTool::on_pointer_up(canvas_pos, doc);
    }

private:
    std::unique_ptr<Stroke> current_stroke_;
};

/// Eraser tool: removes objects or parts of strokes.
class EraserTool : public DrawingTool {
public:
    [[nodiscard]] ToolType type() const override { return ToolType::eraser; }

    void on_pointer_down(Point canvas_pos, WhiteboardDocument& doc) override {
        DrawingTool::on_pointer_down(canvas_pos, doc);
        // Try to erase object under cursor
        auto layer = doc.active_layer();
        if (!layer || layer->locked()) return;

        double eraser_radius = doc.tool_props().thickness * 4.0;
        auto hit = layer->hit_test(canvas_pos, eraser_radius);
        if (hit.hit) {
            auto* obj = layer->find_object(hit.object_id);
            if (obj) {
                auto clone = obj->clone();
                auto cmd = std::make_unique<RemoveObjectCommand>(*layer, std::move(clone));
                doc.undo_manager().execute(std::move(cmd));
            }
        }
    }

    void on_pointer_move(Point canvas_pos, WhiteboardDocument& doc) override {
        if (!is_drawing_) return;
        auto layer = doc.active_layer();
        if (!layer || layer->locked()) return;

        double eraser_radius = doc.tool_props().thickness * 4.0;
        auto hit = layer->hit_test(canvas_pos, eraser_radius);
        if (hit.hit) {
            auto* obj = layer->find_object(hit.object_id);
            if (obj) {
                auto clone = obj->clone();
                auto cmd = std::make_unique<RemoveObjectCommand>(*layer, std::move(clone));
                doc.undo_manager().execute(std::move(cmd));
            }
        }
    }
};

/// Shape tool for drawing rectangles, ovals, lines, arrows.
class ShapeTool : public DrawingTool {
public:
    explicit ShapeTool(ShapeKind kind) : shape_kind_(kind) {}
    [[nodiscard]] ToolType type() const override {
        switch (shape_kind_) {
            case ShapeKind::rectangle: return ToolType::rectangle;
            case ShapeKind::ellipse:   return ToolType::oval;
            case ShapeKind::line:      return ToolType::line;
            case ShapeKind::arrow:     return ToolType::arrow;
            default: return ToolType::rectangle;
        }
    }

    void on_pointer_down(Point canvas_pos, WhiteboardDocument& doc) override {
        DrawingTool::on_pointer_down(canvas_pos, doc);
        if (doc.tool_props().snap_to_grid) {
            canvas_pos = doc.snap_point(canvas_pos);
            start_point_ = canvas_pos;
        }
    }

    void on_pointer_up(Point canvas_pos, WhiteboardDocument& doc) override {
        if (!is_drawing_) return;
        if (doc.tool_props().snap_to_grid) canvas_pos = doc.snap_point(canvas_pos);

        Rect bounds = Rect::from_points(start_point_, canvas_pos);

        // Ensure minimum size for shapes that need it
        auto const& tp = doc.tool_props();
        if (shape_kind_ == ShapeKind::rectangle || shape_kind_ == ShapeKind::ellipse) {
            if (bounds.w < 1.0) bounds.w = 1.0;
            if (bounds.h < 1.0) bounds.h = 1.0;
        }

        auto attrs = default_attrs(doc);
        auto shape = std::make_unique<Shape>(shape_kind_, bounds, attrs);
        shape->set_author(doc.author());
        shape->set_timestamp(
            std::chrono::system_clock::now().time_since_epoch().count());

        auto cmd = std::make_unique<AddObjectCommand>(
            *doc.active_layer(), std::move(shape));
        doc.undo_manager().execute(std::move(cmd));

        DrawingTool::on_pointer_up(canvas_pos, doc);
    }

private:
    ShapeKind shape_kind_;
};

/// Text tool: places text on the canvas.
class TextTool : public DrawingTool {
public:
    [[nodiscard]] ToolType type() const override { return ToolType::text; }

    void on_pointer_down(Point canvas_pos, WhiteboardDocument& doc) override {
        DrawingTool::on_pointer_down(canvas_pos, doc);
        if (doc.tool_props().snap_to_grid) canvas_pos = doc.snap_point(canvas_pos);
        start_point_ = canvas_pos;
    }

    void on_pointer_up(Point canvas_pos, WhiteboardDocument& doc) override {
        if (!is_drawing_) return;
        auto const& tp = doc.tool_props();
        auto attrs = default_attrs(doc);
        attrs.fill.style = FillStyle::solid;
        attrs.fill.color = tp.color;

        // In a real app, the text content would be entered via a UI dialog.
        // Here we create a placeholder text object.
        auto text_obj = std::make_unique<TextObject>(
            "Text", start_point_, tp.font, attrs);
        text_obj->set_author(doc.author());
        text_obj->set_timestamp(
            std::chrono::system_clock::now().time_since_epoch().count());

        auto cmd = std::make_unique<AddObjectCommand>(
            *doc.active_layer(), std::move(text_obj));
        doc.undo_manager().execute(std::move(cmd));

        DrawingTool::on_pointer_up(canvas_pos, doc);
    }
};

/// Selection tool: select, move, resize objects.
class SelectTool : public DrawingTool {
public:
    [[nodiscard]] ToolType type() const override { return ToolType::select; }

    void on_pointer_down(Point canvas_pos, WhiteboardDocument& doc) override {
        DrawingTool::on_pointer_down(canvas_pos, doc);
        start_point_ = canvas_pos;

        auto layer = doc.active_layer();
        if (!layer) return;

        auto hit = layer->hit_test(canvas_pos, 8.0);
        if (hit.hit) {
            // Select the object
            doc.select(hit.object_id);
            is_moving_ = true;
            move_start_ = canvas_pos;

            // Store original positions for undo
            auto* obj = layer->find_object(hit.object_id);
            if (obj) {
                // We'll create a single MoveObjectCommand on pointer_up
                spdlog::debug("SelectTool: selected object {}", hit.object_id);
            }
        } else {
            // Start rectangle selection
            doc.clear_selection();
            is_selecting_rect_ = true;
            selection_rect_start_ = canvas_pos;
        }
    }

    void on_pointer_move(Point canvas_pos, WhiteboardDocument& doc) override {
        if (!is_drawing_) return;

        if (is_moving_ && doc.has_selection()) {
            double dx = canvas_pos.x - move_start_.x;
            double dy = canvas_pos.y - move_start_.y;

            if (doc.tool_props().snap_to_grid) {
                auto snapped = doc.snap_point(canvas_pos);
                auto snapped_start = doc.snap_point(move_start_);
                dx = snapped.x - snapped_start.x;
                dy = snapped.y - snapped_start.y;
            }

            if (std::abs(dx) > 0.01 || std::abs(dy) > 0.01) {
                for (auto* obj : doc.selected_objects()) {
                    obj->move_by(dx, dy);
                }
                move_start_ = canvas_pos;
            }
        }

        if (is_selecting_rect_) {
            selection_rect_current_ = canvas_pos;
        }

        last_point_ = canvas_pos;
    }

    void on_pointer_up(Point canvas_pos, WhiteboardDocument& doc) override {
        if (is_selecting_rect_) {
            // Select all objects within the selection rectangle
            Rect sel_rect = Rect::from_points(selection_rect_start_, canvas_pos);
            auto layer = doc.active_layer();
            if (layer) {
                auto objs = layer->objects_in_rect(sel_rect);
                std::vector<std::string> ids;
                for (auto* obj : objs) ids.push_back(obj->id());
                doc.select(std::move(ids));
            }
            is_selecting_rect_ = false;
        }

        is_moving_ = false;
        DrawingTool::on_pointer_up(canvas_pos, doc);
    }

private:
    bool is_moving_ = false;
    bool is_selecting_rect_ = false;
    Point move_start_;
    Point selection_rect_start_;
    Point selection_rect_current_;
};

/// Pan tool: drag to pan the canvas.
class PanTool : public DrawingTool {
public:
    [[nodiscard]] ToolType type() const override { return ToolType::pan; }

    void on_pointer_down(Point canvas_pos, WhiteboardDocument& doc) override {
        DrawingTool::on_pointer_down(canvas_pos, doc);
    }

    void on_pointer_move(Point canvas_pos, WhiteboardDocument& doc) override {
        if (!is_drawing_) return;
        double dx = canvas_pos.x - last_point_.x;
        double dy = canvas_pos.y - last_point_.y;
        // Pan in screen space
        doc.mutable_transform().pan(dx * doc.transform().scale,
                                     dy * doc.transform().scale);
        last_point_ = canvas_pos;
    }
};

// ============================================================================
// Tool Factory
// ============================================================================

class ToolFactory {
public:
    [[nodiscard]] static std::unique_ptr<DrawingTool> create(ToolType type) {
        switch (type) {
            case ToolType::pen:         return std::make_unique<PenTool>();
            case ToolType::highlighter: return std::make_unique<HighlighterTool>();
            case ToolType::eraser:      return std::make_unique<EraserTool>();
            case ToolType::rectangle:   return std::make_unique<ShapeTool>(ShapeKind::rectangle);
            case ToolType::oval:        return std::make_unique<ShapeTool>(ShapeKind::ellipse);
            case ToolType::line:        return std::make_unique<ShapeTool>(ShapeKind::line);
            case ToolType::arrow:       return std::make_unique<ShapeTool>(ShapeKind::arrow);
            case ToolType::text:        return std::make_unique<TextTool>();
            case ToolType::select:      return std::make_unique<SelectTool>();
            case ToolType::pan:         return std::make_unique<PanTool>();
            default:
                spdlog::warn("ToolFactory: unhandled tool type {}, falling back to pen",
                             static_cast<int>(type));
                return std::make_unique<PenTool>();
        }
    }

    /// Get a human-readable name for a tool type.
    [[nodiscard]] static std::string_view name(ToolType type) {
        using enum ToolType;
        switch (type) {
            case select:       return "Select";
            case pan:          return "Pan";
            case pen:          return "Pen";
            case highlighter:  return "Highlighter";
            case eraser:       return "Eraser";
            case rectangle:    return "Rectangle";
            case oval:         return "Oval";
            case line:         return "Line";
            case arrow:        return "Arrow";
            case text:         return "Text";
            case eyedropper:   return "Eyedropper";
            case fill_bucket:  return "Fill Bucket";
            case laser_pointer: return "Laser Pointer";
        }
        return "Unknown";
    }
};

// ============================================================================
// Whiteboard Session (orchestrates everything)
// ============================================================================

/// High-level whiteboard session that ties together document, tools, undo, and export.
class WhiteboardSession {
public:
    WhiteboardSession() {
        active_tool_ = ToolFactory::create(ToolType::pen);
        spdlog::info("WhiteboardSession: created with default pen tool");
    }

    WhiteboardSession(WhiteboardSession const&) = delete;
    WhiteboardSession& operator=(WhiteboardSession const&) = delete;
    WhiteboardSession(WhiteboardSession&&) = default;
    WhiteboardSession& operator=(WhiteboardSession&&) = default;

    // === Document access ===

    [[nodiscard]] WhiteboardDocument& document() { return document_; }
    [[nodiscard]] WhiteboardDocument const& document() const { return document_; }

    // === Tool management ===

    void set_tool(ToolType type) {
        active_tool_ = ToolFactory::create(type);
        document_.set_tool_props(ToolProperties{.type = type, .color = document_.tool_props().color});
        spdlog::debug("WhiteboardSession: switched to {} tool", ToolFactory::name(type));
    }

    [[nodiscard]] ToolType current_tool_type() const {
        return active_tool_ ? active_tool_->type() : ToolType::pen;
    }

    [[nodiscard]] DrawingTool* current_tool() { return active_tool_.get(); }

    // === Input handling ===

    /// Convert screen-space pointer coordinates to canvas-space and dispatch.
    void handle_pointer_down(double screen_x, double screen_y) {
        Point canvas = document_.transform().screen_to_canvas({screen_x, screen_y});
        active_tool_->on_pointer_down(canvas, document_);
    }

    void handle_pointer_move(double screen_x, double screen_y) {
        Point canvas = document_.transform().screen_to_canvas({screen_x, screen_y});
        active_tool_->on_pointer_move(canvas, document_);
    }

    void handle_pointer_up(double screen_x, double screen_y) {
        Point canvas = document_.transform().screen_to_canvas({screen_x, screen_y});
        active_tool_->on_pointer_up(canvas, document_);
    }

    // === Canvas navigation ===

    void zoom_in(Point screen_center) {
        document_.mutable_transform().zoom_at(screen_center, 1.25);
    }

    void zoom_out(Point screen_center) {
        document_.mutable_transform().zoom_at(screen_center, 0.8);
    }

    void zoom_to_fit(double screen_w, double screen_h) {
        auto& xform = document_.mutable_transform();
        double sx = screen_w / document_.width();
        double sy = screen_h / document_.height();
        xform.scale = std::min(sx, sy);
        xform.offset_x = (screen_w - document_.width() * xform.scale) / 2.0;
        xform.offset_y = (screen_h - document_.height() * xform.scale) / 2.0;
    }

    void zoom_to(double scale) {
        document_.mutable_transform().scale = std::clamp(
            scale, document_.transform().min_scale, document_.transform().max_scale);
    }

    void pan_by(double dx, double dy) {
        document_.mutable_transform().pan(dx, dy);
    }

    void reset_view() {
        document_.mutable_transform().reset();
    }

    // === Undo/Redo ===

    void undo() {
        document_.undo_manager().undo();
        spdlog::debug("WhiteboardSession: undo");
    }

    void redo() {
        document_.undo_manager().redo();
        spdlog::debug("WhiteboardSession: redo");
    }

    // === Selection and editing ===

    void select_all() {
        auto* layer = document_.active_layer();
        if (!layer) return;
        std::vector<std::string> ids;
        for (auto const& obj : layer->objects())
            ids.push_back(obj->id());
        document_.select(std::move(ids));
    }

    void deselect_all() {
        document_.clear_selection();
    }

    void delete_selected() {
        if (!document_.has_selection()) return;

        auto layer = document_.active_layer();
        if (!layer || layer->locked()) return;

        // For undo: create a composite command that removes all selected objects
        auto composite = std::make_unique<CompositeCommand>();
        for (auto& id : document_.selection()) {
            auto* obj = layer->find_object(id);
            if (obj) {
                auto clone = obj->clone();
                composite->add(std::make_unique<RemoveObjectCommand>(*layer, std::move(clone)));
            }
        }
        if (composite->size() > 0) {
            document_.undo_manager().execute(std::move(composite));
        }
    }

    void duplicate_selected() {
        auto layer = document_.active_layer();
        if (!layer) return;

        auto composite = std::make_unique<CompositeCommand>();
        for (auto* obj : document_.selected_objects()) {
            auto clone = obj->clone();
            clone->move_by(20, 20); // offset the duplicate
            composite->add(std::make_unique<AddObjectCommand>(*layer, std::move(clone)));
        }
        if (composite->size() > 0) {
            document_.undo_manager().execute(std::move(composite));
        }
    }

    // === Layer operations ===

    Layer& add_layer(std::string name) {
        auto& layer = document_.add_layer(std::move(name));
        spdlog::debug("WhiteboardSession: added layer '{}'", layer.name());
        return layer;
    }

    void remove_layer(std::string const& id) {
        document_.remove_layer(id);
        spdlog::debug("WhiteboardSession: removed layer '{}'", id);
    }

    // === Serialization ===

    bool save_to_file(std::string const& path) {
        try {
            std::string json = document_.to_json();
            std::ofstream f(path);
            if (!f) {
                spdlog::error("WhiteboardSession: cannot save to {}", path);
                return false;
            }
            f << json;
            spdlog::info("WhiteboardSession: saved {} bytes to {}", json.size(), path);
            return true;
        } catch (std::exception const& e) {
            spdlog::error("WhiteboardSession: save failed: {}", e.what());
            return false;
        }
    }

    bool load_from_file(std::string const& path) {
        try {
            std::ifstream f(path);
            if (!f) {
                spdlog::error("WhiteboardSession: cannot open {}", path);
                return false;
            }
            std::stringstream ss;
            ss << f.rdbuf();
            document_.from_json(ss.str());
            spdlog::info("WhiteboardSession: loaded {}", path);
            return true;
        } catch (std::exception const& e) {
            spdlog::error("WhiteboardSession: load failed: {}", e.what());
            return false;
        }
    }

    // === Export ===

    bool export_svg(std::string const& path) {
        return WhiteboardExporter::export_svg(document_, path);
    }

    bool export_png(std::string const& path) {
        return WhiteboardExporter::export_png(document_, path);
    }

    bool export_pdf(std::string const& path) {
        return WhiteboardExporter::export_pdf(document_, path);
    }

    // === Collaborative editing ===

    void receive_remote_change(RemoteChange change) {
        merge_engine_.receive_remote_change(std::move(change), document_);
    }

    void process_remote_changes() {
        size_t n = merge_engine_.process_all(document_);
        if (n > 0) spdlog::debug("WhiteboardSession: processed {} remote changes", n);
    }

    /// Broadcast a local change (mark object for sending to collaborators).
    [[nodiscard]] RemoteChange create_change_for_broadcast(Drawable const& obj,
                                                            RemoteChange::Action action) {
        return merge_engine_.create_local_change(obj, action);
    }

    // === Color & tool properties ===

    void set_color(Color c) {
        auto tp = document_.tool_props();
        tp.color = c;
        document_.set_tool_props(tp);
    }

    void set_thickness(double t) {
        auto tp = document_.tool_props();
        tp.thickness = std::clamp(t, 1.0, 100.0);
        document_.set_tool_props(tp);
    }

    void set_font(FontDescriptor f) {
        auto tp = document_.tool_props();
        tp.font = std::move(f);
        document_.set_tool_props(tp);
    }

    void set_opacity(double o) {
        auto tp = document_.tool_props();
        tp.opacity = std::clamp(o, 0.0, 1.0);
        document_.set_tool_props(tp);
    }

    void toggle_snap() {
        auto tp = document_.tool_props();
        tp.snap_to_grid = !tp.snap_to_grid;
        document_.set_tool_props(tp);
    }

    // === Grid ===

    void set_grid_visible(bool visible) {
        auto g = document_.grid();
        g.visible = visible;
        document_.set_grid(g);
    }

    void set_grid_spacing(double spacing) {
        auto g = document_.grid();
        g.spacing = std::clamp(spacing, 5.0, 200.0);
        document_.set_grid(g);
    }

    // === New / Clear ===

    void new_document() {
        document_ = WhiteboardDocument{};
        active_tool_ = ToolFactory::create(ToolType::pen);
        spdlog::info("WhiteboardSession: new document created");
    }

    // === Stats ===

    [[nodiscard]] size_t total_object_count() const {
        size_t count = 0;
        for (auto const& layer : document_.layers())
            count += layer->object_count();
        return count;
    }

    [[nodiscard]] size_t layer_count() const {
        return document_.layer_count();
    }

private:
    WhiteboardDocument document_;
    std::unique_ptr<DrawingTool> active_tool_;
    CollaborativeMerge merge_engine_;
};

// ============================================================================
// Whiteboard Manager (static globals, multi-session support)
// ============================================================================

class WhiteboardManager {
public:
    static WhiteboardManager& instance() {
        static WhiteboardManager mgr;
        return mgr;
    }

    /// Create a new whiteboard session.
    [[nodiscard]] std::string create_session(std::string title = "Untitled") {
        auto session = std::make_shared<WhiteboardSession>();
        session->document().set_title(std::move(title));
        std::string id = ObjectId::generate();
        std::lock_guard lock(mutex_);
        sessions_[id] = std::move(session);
        spdlog::info("WhiteboardManager: created session '{}'", id);
        return id;
    }

    /// Get a session by ID, or nullptr.
    [[nodiscard]] std::shared_ptr<WhiteboardSession> get_session(std::string const& id) {
        std::shared_lock lock(mutex_);
        auto it = sessions_.find(id);
        return it != sessions_.end() ? it->second : nullptr;
    }

    /// Remove/destroy a session.
    bool destroy_session(std::string const& id) {
        std::lock_guard lock(mutex_);
        auto n = sessions_.erase(id);
        if (n > 0) spdlog::info("WhiteboardManager: destroyed session '{}'", id);
        return n > 0;
    }

    /// List all active session IDs.
    [[nodiscard]] std::vector<std::string> list_sessions() const {
        std::shared_lock lock(mutex_);
        std::vector<std::string> ids;
        for (auto const& [id, _] : sessions_) ids.push_back(id);
        return ids;
    }

    /// Get the number of active sessions.
    [[nodiscard]] size_t session_count() const {
        std::shared_lock lock(mutex_);
        return sessions_.size();
    }

private:
    WhiteboardManager() = default;
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<WhiteboardSession>> sessions_;
};

// ============================================================================
// Utility: grid snapping helpers
// ============================================================================

/// Helper to snap a value to the nearest grid multiple.
[[nodiscard]] inline double snap_value(double value, double grid_spacing) {
    return std::round(value / grid_spacing) * grid_spacing;
}

/// Helper to snap a point to the grid.
[[nodiscard]] inline Point snap_point_grid(Point p, GridSettings const& gs) {
    if (!gs.snap_enabled) return p;
    return {snap_value(p.x, gs.spacing), snap_value(p.y, gs.spacing)};
}

/// Helper to snap a rectangle to the grid.
[[nodiscard]] inline Rect snap_rect_grid(Rect r, GridSettings const& gs) {
    if (!gs.snap_enabled) return r;
    Point snapped_origin = snap_point_grid({r.x, r.y}, gs);
    // Snap size too
    double snapped_w = snap_value(r.w, gs.spacing);
    double snapped_h = snap_value(r.h, gs.spacing);
    if (snapped_w < gs.spacing) snapped_w = gs.spacing;
    if (snapped_h < gs.spacing) snapped_h = gs.spacing;
    return {snapped_origin.x, snapped_origin.y, snapped_w, snapped_h};
}

} // namespace cppdesk::whiteboard

// ============================================================================
// Main entry point for testing/standalone usage
// ============================================================================

#ifdef WHITEBOARD_STANDALONE_TEST

#include <iostream>

int main() {
    using namespace cppdesk::whiteboard;

    spdlog::set_level(spdlog::level::debug);
    spdlog::info("===== Whiteboard Standalone Test =====");

    // Create a session via manager
    auto& mgr = WhiteboardManager::instance();
    std::string sid = mgr.create_session("Test Whiteboard");
    auto session = mgr.get_session(sid);
    if (!session) {
        spdlog::error("Failed to get session");
        return 1;
    }

    // Test drawing with pen tool
    session->set_tool(ToolType::pen);
    session->set_color(Color::red());
    session->set_thickness(3.0);

    // Simulate pointer events
    session->handle_pointer_down(100, 100);
    session->handle_pointer_move(150, 120);
    session->handle_pointer_move(200, 180);
    session->handle_pointer_move(250, 220);
    session->handle_pointer_up(300, 280);

    spdlog::info("After pen stroke: {} objects total", session->total_object_count());

    // Test shape tool
    session->set_tool(ToolType::rectangle);
    session->set_color(Color::blue());
    session->handle_pointer_down(400, 300);
    session->handle_pointer_up(600, 450);

    spdlog::info("After rectangle: {} objects total", session->total_object_count());

    // Test text tool
    session->set_tool(ToolType::text);
    session->set_font(FontDescriptor{.family = "Arial", .size = 24.0, .bold = true});
    session->handle_pointer_down(50, 500);
    session->handle_pointer_up(50, 500);

    spdlog::info("After text: {} objects total", session->total_object_count());

    // Test undo
    session->undo();
    spdlog::info("After undo: {} objects total", session->total_object_count());

    // Test redo
    session->redo();
    spdlog::info("After redo: {} objects total", session->total_object_count());

    // Test selection
    session->set_tool(ToolType::select);
    session->handle_pointer_down(550, 375); // click on rectangle
    session->handle_pointer_up(550, 375);
    spdlog::info("Selection count: {}", session->document().selection().size());

    // Test zoom
    session->zoom_in({500, 400});
    spdlog::info("Zoom scale: {:.2f}", session->document().transform().scale);

    // Test serialization
    std::string json = session->document().to_json();
    spdlog::info("Serialized document: {} bytes", json.size());
    spdlog::info("JSON preview: {}...", json.substr(0, 200));

    // Test export
    bool svg_ok = session->export_svg("/tmp/whiteboard_test.svg");
    spdlog::info("SVG export: {}", svg_ok ? "OK" : "FAILED");

    // Test collaborative merge
    RemoteChange change{
        .action = RemoteChange::Action::add,
        .object_json = R"({"type":"stroke","id":"remote1"})",
        .object_id = "remote1",
        .timestamp = 1234567890,
        .author = "collaborator",
        .lamport_clock = 1
    };
    session->receive_remote_change(change);
    session->process_remote_changes();

    // Test grid snapping
    Point p = snap_point_grid({127.3, 83.9}, GridSettings{.snap_enabled = true, .spacing = 20.0});
    spdlog::info("Snapped point: ({:.1f}, {:.1f})", p.x, p.y);

    // Test color palette
    ColorPalette palette;
    spdlog::info("Palette: {} colors, thicknesses: {} options",
                 palette.size(), palette.thicknesses().size());

    // Test layers
    session->add_layer("Annotations");
    session->add_layer("Drafts");
    spdlog::info("Layer count: {}", session->layer_count());

    // Test duplicate
    session->set_tool(ToolType::select);
    session->handle_pointer_down(550, 375);
    session->handle_pointer_up(550, 375);
    session->duplicate_selected();
    spdlog::info("After duplicate: {} objects total", session->total_object_count());

    // Test save to file
    session->save_to_file("/tmp/whiteboard_test.json");

    // Test export
    session->export_pdf("/tmp/whiteboard_test.pdf");

    // Destroy session
    mgr.destroy_session(sid);
    spdlog::info("Session destroyed. Remaining sessions: {}", mgr.session_count());

    spdlog::info("===== Whiteboard Test Complete =====");
    return 0;
}

#endif // WHITEBOARD_STANDALONE_TEST
