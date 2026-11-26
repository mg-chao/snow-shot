mod thought_bubble;

use std::cell::RefCell;
use std::collections::HashMap;
use std::hash::{Hash, Hasher};
use std::sync::Arc;

use serde::{Deserialize, Serialize};
use snow_draw_engine_core::{CornerRadii, DrawRect, ErrorCode, PathCommand, PathGeometry, Point};

use crate::{ElementId, HighlightShape, RectangleData, RectangleElementKind};

pub const SPEECH_BUBBLE_MIN_BASE_WIDTH: f64 = 0.001;
const SPEECH_BUBBLE_MIN_HALF_ANGLE: f64 = 0.001;
pub const SPEECH_BUBBLE_MAX_HALF_ANGLE: f64 = std::f64::consts::FRAC_PI_3;

/// A tail tip is stored in the bubble's unrotated, center-relative coordinates.
/// Body edits reproject it to keep its canvas position fixed.
/// The requested join width is measured in canvas units, independent of the body center.
/// Attachment rays remain symmetric around the center-to-tip axis.
#[derive(Clone, Copy, Debug, PartialEq, Serialize, Deserialize)]
pub struct SpeechBubbleTail {
    pub id: u64,
    pub tip: Point<f64>,
    pub base_width: f64,
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(try_from = "SerializedSpeechBubble")]
pub struct SpeechBubbleData {
    pub body: RectangleData,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub text_element_id: Option<ElementId>,
    pub tails: Vec<SpeechBubbleTail>,
}

// Older sessions stored an angular spread. Convert it once on import using
// that session's contour, so subsequent moves and resizes retain a distance.
#[derive(Deserialize)]
struct SerializedSpeechBubble {
    body: RectangleData,
    text_element_id: Option<ElementId>,
    tails: Vec<SerializedTail>,
}

#[derive(Deserialize)]
struct SerializedTail {
    id: u64,
    tip: Point<f64>,
    base_width: Option<f64>,
    half_angle: Option<f64>,
}

impl TryFrom<SerializedSpeechBubble> for SpeechBubbleData {
    type Error = &'static str;

    fn try_from(saved: SerializedSpeechBubble) -> Result<Self, Self::Error> {
        let mut value = Self::new(saved.body);
        value.text_element_id = saved.text_element_id;
        let mut legacy_body = None;
        for tail in saved.tails {
            let base_width = if let Some(width) = tail.base_width {
                width
            } else if let Some(angle) = tail.half_angle {
                if !angle.is_finite()
                    || !(SPEECH_BUBBLE_MIN_HALF_ANGLE..=SPEECH_BUBBLE_MAX_HALF_ANGLE)
                        .contains(&angle)
                {
                    return Err("invalid speech bubble tail angle");
                }
                let body = legacy_body
                    .get_or_insert_with(|| body_segments(speech_bubble_body_proxy(&value)));
                let width = base_width_at_angle(body, tail.tip.y.atan2(tail.tip.x), angle);
                if !width.is_finite() {
                    return Err("invalid speech bubble tail contour");
                }
                width.max(SPEECH_BUBBLE_MIN_BASE_WIDTH)
            } else {
                return Err("missing speech bubble tail base_width");
            };
            value.tails.push(SpeechBubbleTail {
                id: tail.id,
                tip: tail.tip,
                base_width,
            });
        }
        Ok(value)
    }
}

impl SpeechBubbleData {
    pub fn new(mut body: RectangleData) -> Self {
        body.text_element_id = None;
        Self {
            body,
            text_element_id: None,
            tails: Vec::new(),
        }
    }

    pub fn next_tail_id(&self) -> u64 {
        self.tails
            .iter()
            .map(|tail| tail.id)
            .max()
            .unwrap_or(0)
            .saturating_add(1)
    }
}

/// The rendered body uses the same effective corner geometry for all consumers.
/// Diamond rounding is capped so its centered half-size text box remains inside.
pub fn speech_bubble_body_proxy(value: &SpeechBubbleData) -> RectangleData {
    let mut body = value.body;
    body.text_element_id = None;
    if body.highlight_shape != HighlightShape::Ellipse {
        let divisor = if body.highlight_shape == HighlightShape::Diamond {
            4.0
        } else {
            2.0
        };
        let limit = body.width.min(body.height).max(0.0) / divisor;
        body.corner_radii = CornerRadii {
            top_left: body.corner_radii.top_left.min(limit),
            top_right: body.corner_radii.top_right.min(limit),
            bottom_right: body.corner_radii.bottom_right.min(limit),
            bottom_left: body.corner_radii.bottom_left.min(limit),
        };
    }
    if matches!(
        body.highlight_shape,
        HighlightShape::Ellipse | HighlightShape::ThoughtBubble
    ) {
        body.corner_radii = CornerRadii::default();
    } else {
        body.corner_radii = crate::normalize_shape_corner_radii(
            body.highlight_shape,
            body.width,
            body.height,
            body.corner_radii,
        );
    }
    body
}

pub fn validate_speech_bubble(value: &SpeechBubbleData) -> Result<(), ErrorCode> {
    // Bubble radii retain the requested setting when the body becomes smaller.
    // Only the effective contour must fit; reject invalid raw values before the
    // cap can replace them (notably NaN, which f64::min would otherwise discard).
    if [
        value.body.corner_radii.top_left,
        value.body.corner_radii.top_right,
        value.body.corner_radii.bottom_right,
        value.body.corner_radii.bottom_left,
    ]
    .iter()
    .any(|radius| !radius.is_finite() || *radius < 0.0)
    {
        return Err(ErrorCode::InvalidArgument);
    }
    crate::document_geometry::validate_rectangle_properties(&speech_bubble_body_proxy(value))?;
    if value.body.rectangle_kind != RectangleElementKind::Rectangle
        || value.body.text_element_id.is_some()
        || value.body.width <= 0.0
        || value.body.height <= 0.0
    {
        return Err(ErrorCode::InvalidArgument);
    }
    let mut ids = std::collections::HashSet::with_capacity(value.tails.len());
    for tail in &value.tails {
        if tail.id == 0
            || !ids.insert(tail.id)
            || !tail.tip.x.is_finite()
            || !tail.tip.y.is_finite()
            || !tail.base_width.is_finite()
            || tail.base_width < SPEECH_BUBBLE_MIN_BASE_WIDTH
            || speech_bubble_body_contains(value, tail.tip)
        {
            return Err(ErrorCode::InvalidArgument);
        }
    }
    Ok(())
}

pub fn speech_bubble_local_to_canvas(value: &SpeechBubbleData, point: Point<f64>) -> Point<f64> {
    let (sin, cos) = value.body.rotation.sin_cos();
    Point::new(
        value.body.center.x + point.x * cos - point.y * sin,
        value.body.center.y + point.x * sin + point.y * cos,
    )
}

pub fn speech_bubble_canvas_to_local(value: &SpeechBubbleData, point: Point<f64>) -> Point<f64> {
    let (sin, cos) = value.body.rotation.sin_cos();
    let x = point.x - value.body.center.x;
    let y = point.y - value.body.center.y;
    Point::new(x * cos + y * sin, -x * sin + y * cos)
}

/// Tests the strict interior, allowing a zero-length tail exactly on the contour.
/// The argument is a local point, so callers can use this during captured drags.
pub fn speech_bubble_body_contains(value: &SpeechBubbleData, local: Point<f64>) -> bool {
    if !local.x.is_finite() || !local.y.is_finite() {
        return false;
    }
    if local.x == 0.0 && local.y == 0.0 {
        return true;
    }
    let body = body_segments(speech_bubble_body_proxy(value));
    point_inside_body(&body, local)
}

pub fn speech_bubble_tail_base_points(
    value: &SpeechBubbleData,
    tail: &SpeechBubbleTail,
) -> [Point<f64>; 2] {
    if let Some(index) = value.tails.iter().position(|existing| existing == tail) {
        let geometry = cached_geometry(value);
        return if value.body.highlight_shape == HighlightShape::ThoughtBubble {
            geometry.tail_bases[index]
        } else {
            let triangle = geometry.triangles[index];
            [triangle[0], triangle[2]]
        };
    }
    let body = body_segments(speech_bubble_body_proxy(value));
    if value.body.highlight_shape == HighlightShape::ThoughtBubble {
        thought_bubble::base_points(value, &body, tail)
    } else {
        tail_bases(&body, tail)
    }
}

pub fn speech_bubble_tail_effective_half_angle(
    value: &SpeechBubbleData,
    tail: &SpeechBubbleTail,
) -> f64 {
    let bases = speech_bubble_tail_base_points(value, tail);
    let axis = tail.tip.y.atan2(tail.tip.x);
    let angle = bases[0].y.atan2(bases[0].x) - axis;
    ((angle + std::f64::consts::PI).rem_euclid(std::f64::consts::TAU) - std::f64::consts::PI).abs()
}

pub fn speech_bubble_default_tail(
    value: &SpeechBubbleData,
    tip: Point<f64>,
    zoom: f64,
) -> SpeechBubbleTail {
    SpeechBubbleTail {
        id: value.next_tail_id(),
        tip,
        base_width: (32.0 / zoom.max(0.0001))
            .min(value.body.width / 4.0)
            .max(SPEECH_BUBBLE_MIN_BASE_WIDTH),
    }
}

pub fn speech_bubble_base_width_from_point(
    value: &SpeechBubbleData,
    tail: &SpeechBubbleTail,
    point: Point<f64>,
) -> f64 {
    if value.body.highlight_shape == HighlightShape::ThoughtBubble {
        let length = tail.tip.x.hypot(tail.tip.y).max(f64::EPSILON);
        return (2.0 * cross(tail.tip, point).abs() / length).max(SPEECH_BUBBLE_MIN_BASE_WIDTH);
    }
    if point.x == 0.0 && point.y == 0.0 {
        return tail.base_width;
    }
    let axis = tail.tip.y.atan2(tail.tip.x);
    let angle = point.y.atan2(point.x) - axis;
    let spread = ((angle + std::f64::consts::PI).rem_euclid(std::f64::consts::TAU)
        - std::f64::consts::PI)
        .abs()
        .min(SPEECH_BUBBLE_MAX_HALF_ANGLE);
    let body = body_segments(speech_bubble_body_proxy(value));
    base_width_at_angle(&body, axis, spread).max(SPEECH_BUBBLE_MIN_BASE_WIDTH)
}

/// Shape/radius changes can cover a tip just like automatic growth can.
pub fn speech_bubble_remove_swallowed_tails(value: &mut SpeechBubbleData) -> bool {
    let body = body_segments(speech_bubble_body_proxy(value));
    let count = value.tails.len();
    value
        .tails
        .retain(|tail| !point_inside_body(&body, tail.tip));
    value.tails.len() != count
}

/// Grow downward in the body's axes. Existing tail tips retain their canvas positions.
pub fn speech_bubble_grow_height(value: &SpeechBubbleData, height: f64) -> SpeechBubbleData {
    speech_bubble_grow_size(value, value.body.width, height)
}

/// Grow from the body's local top-left corner, preserving all tail canvas positions.
pub fn speech_bubble_grow_size(
    value: &SpeechBubbleData,
    width: f64,
    height: f64,
) -> SpeechBubbleData {
    if !width.is_finite() || !height.is_finite() {
        return value.clone();
    }
    let width = width.max(value.body.width);
    let height = height.max(value.body.height);
    if width == value.body.width && height == value.body.height {
        return value.clone();
    }
    let mut next = value.clone();
    let dx = (width - value.body.width) / 2.0;
    let dy = (height - value.body.height) / 2.0;
    let (sin, cos) = value.body.rotation.sin_cos();
    next.body.center.x += dx * cos - dy * sin;
    next.body.center.y += dx * sin + dy * cos;
    next.body.width = width;
    next.body.height = height;
    for tail in &mut next.tails {
        tail.tip.x -= dx;
        tail.tip.y -= dy;
    }
    speech_bubble_remove_swallowed_tails(&mut next);
    next
}

/// Change the body while preserving tail tips in canvas coordinates.
/// The target rectangle is the unpainted body, not the tail-inclusive paint bounds.
pub fn speech_bubble_with_selection_rect(
    value: &SpeechBubbleData,
    rect: RectangleData,
) -> SpeechBubbleData {
    let mut next = value.clone();
    next.body = rect;
    next.body.text_element_id = None;
    next.body.rectangle_kind = RectangleElementKind::Rectangle;
    if next.body.center != value.body.center || next.body.rotation != value.body.rotation {
        let frame = SpeechBubbleData::new(next.body);
        for tail in &mut next.tails {
            let canvas_tip = speech_bubble_local_to_canvas(value, tail.tip);
            tail.tip = speech_bubble_canvas_to_local(&frame, canvas_tip);
        }
    }
    speech_bubble_remove_swallowed_tails(&mut next);
    next
}

pub fn speech_bubble_local_path_geometry(value: &SpeechBubbleData) -> PathGeometry {
    cached_geometry(value).path.clone()
}

pub fn speech_bubble_path_geometry(value: &SpeechBubbleData) -> PathGeometry {
    let local = cached_geometry(value);
    let map = |p: [f64; 2]| {
        let p = speech_bubble_local_to_canvas(value, Point::new(p[0], p[1]));
        [p.x, p.y]
    };
    let commands = local
        .path
        .flattened_commands()
        .into_iter()
        .map(|command| match command {
            PathCommand::MoveTo { point } => PathCommand::MoveTo { point: map(point) },
            PathCommand::LineTo { point } => PathCommand::LineTo { point: map(point) },
            PathCommand::QuadTo { control, end } => PathCommand::QuadTo {
                control: map(control),
                end: map(end),
            },
            PathCommand::CubicTo {
                control_1,
                control_2,
                end,
            } => PathCommand::CubicTo {
                control_1: map(control_1),
                control_2: map(control_2),
                end: map(end),
            },
        })
        .collect();
    let mut revision = std::collections::hash_map::DefaultHasher::new();
    local.path.revision.hash(&mut revision);
    value.body.center.x.to_bits().hash(&mut revision);
    value.body.center.y.to_bits().hash(&mut revision);
    value.body.rotation.to_bits().hash(&mut revision);
    PathGeometry::from_commands(revision.finish(), commands, true)
}

pub fn speech_bubble_bounds(value: &SpeechBubbleData) -> DrawRect {
    let bounds = speech_bubble_path_geometry(value).canvas_bounds;
    let outset = if value.body.stroke.a != 0 {
        value.body.stroke_width / 2.0
    } else {
        0.0
    } + speech_bubble_stroke_rendering_outset(value);
    DrawRect::new(
        bounds[0] - outset,
        bounds[1] - outset,
        bounds[2] + outset,
        bounds[3] + outset,
    )
}

fn speech_bubble_stroke_rendering_outset(value: &SpeechBubbleData) -> f64 {
    if value.body.stroke.a == 0 || value.body.stroke_width <= 0.0 {
        return 0.0;
    }
    snow_draw_engine_core::arrow::stroke_rendering_outset(
        value.body.stroke_rendering_style,
        f64::MAX,
    )
}

pub fn speech_bubble_hit_test(value: &SpeechBubbleData, point: Point<f64>, tolerance: f64) -> bool {
    let point = speech_bubble_canvas_to_local(value, point);
    if !point.x.is_finite() || !point.y.is_finite() {
        return false;
    }
    let geometry = cached_geometry(value);
    let has_stroke = value.body.stroke.a != 0 && value.body.stroke_width > 0.0;
    let has_fill = value.body.fill_style.is_visible(value.body.fill);
    if (has_fill || has_stroke) && point_inside_body(&geometry.body, point) {
        return true;
    }
    if (has_fill || has_stroke)
        && geometry.circles.iter().any(|circle| {
            (point.x - circle.center.x).hypot(point.y - circle.center.y) <= circle.radius
        })
    {
        return true;
    }
    if has_fill
        && geometry
            .triangles
            .iter()
            .any(|triangle| triangle_contains(*triangle, point, false))
    {
        return true;
    }
    if !has_fill && !has_stroke {
        return false;
    }
    let distance = if has_stroke {
        value.body.stroke_width / 2.0
    } else {
        0.0
    } + tolerance.max(0.0)
        + speech_bubble_stroke_rendering_outset(value);
    geometry
        .outline
        .iter()
        .any(|segment| segment_near_point(*segment, point, distance, 0))
}

#[derive(Clone, Debug, PartialEq, Eq, Hash)]
struct GeometryKey {
    shape: u8,
    stroke_width: u64,
    size: [u64; 2],
    radii: [u64; 4],
    tails: Vec<[u64; 4]>,
}

impl GeometryKey {
    fn new(value: &SpeechBubbleData) -> Self {
        let body = speech_bubble_body_proxy(value);
        Self {
            shape: body.highlight_shape as u8,
            stroke_width: if body.highlight_shape == HighlightShape::ThoughtBubble {
                body.stroke_width.to_bits()
            } else {
                0
            },
            size: [body.width.to_bits(), body.height.to_bits()],
            radii: [
                body.corner_radii.top_left.to_bits(),
                body.corner_radii.top_right.to_bits(),
                body.corner_radii.bottom_right.to_bits(),
                body.corner_radii.bottom_left.to_bits(),
            ],
            tails: value
                .tails
                .iter()
                .map(|tail| {
                    [
                        tail.id,
                        tail.tip.x.to_bits(),
                        tail.tip.y.to_bits(),
                        tail.base_width.to_bits(),
                    ]
                })
                .collect(),
        }
    }
}

#[derive(Default)]
struct GeometryCache {
    entries: HashMap<GeometryKey, GeometryCacheEntry>,
    clock: u64,
    builds: u64,
    retained_bytes: usize,
}

const GEOMETRY_CACHE_BYTE_BUDGET: usize = 4 * 1024 * 1024;

struct GeometryCacheEntry {
    geometry: Arc<BubbleGeometry>,
    used: u64,
    bytes: usize,
}

impl GeometryCache {
    fn retain(&mut self, key: GeometryKey, geometry: Arc<BubbleGeometry>, used: u64) {
        let bytes = geometry_bytes(&key, &geometry);
        if bytes > GEOMETRY_CACHE_BYTE_BUDGET {
            return;
        }
        while self.entries.len() >= 128 || self.retained_bytes + bytes > GEOMETRY_CACHE_BYTE_BUDGET
        {
            let Some(oldest) = self
                .entries
                .iter()
                .min_by_key(|(_, entry)| entry.used)
                .map(|(key, _)| key.clone())
            else {
                break;
            };
            if let Some(entry) = self.entries.remove(&oldest) {
                self.retained_bytes -= entry.bytes;
            }
        }
        self.retained_bytes += bytes;
        self.entries.insert(
            key,
            GeometryCacheEntry {
                geometry,
                used,
                bytes,
            },
        );
    }
}

thread_local! {
    static GEOMETRY_CACHE: RefCell<GeometryCache> = RefCell::new(GeometryCache::default());
}

/// Releases derived bubble geometry after a document has finished rendering on this thread.
pub fn clear_speech_bubble_geometry_cache_for_current_thread() {
    GEOMETRY_CACHE.with(|cache| {
        let mut cache = cache.borrow_mut();
        *cache = GeometryCache {
            builds: cache.builds,
            ..GeometryCache::default()
        };
    });
}

pub fn speech_bubble_geometry_build_count() -> u64 {
    GEOMETRY_CACHE.with(|cache| cache.borrow().builds)
}

pub fn speech_bubble_geometry_retained_bytes() -> usize {
    GEOMETRY_CACHE.with(|cache| cache.borrow().retained_bytes)
}

struct BubbleGeometry {
    body: Vec<Segment>,
    triangles: Vec<[Point<f64>; 3]>,
    tail_bases: Vec<[Point<f64>; 2]>,
    circles: Vec<thought_bubble::Circle>,
    outline: Vec<Segment>,
    path: PathGeometry,
}

fn geometry_bytes(key: &GeometryKey, geometry: &BubbleGeometry) -> usize {
    use std::mem::size_of;
    size_of::<GeometryKey>()
        + key.tails.capacity() * size_of::<[u64; 4]>()
        + size_of::<BubbleGeometry>()
        + size_of::<GeometryCacheEntry>()
        + 64
        + geometry.body.capacity() * size_of::<Segment>()
        + geometry.triangles.capacity() * size_of::<[Point<f64>; 3]>()
        + geometry.tail_bases.capacity() * size_of::<[Point<f64>; 2]>()
        + geometry.circles.capacity() * size_of::<thought_bubble::Circle>()
        + geometry.outline.capacity() * size_of::<Segment>()
        + geometry.path.chunks.len() * size_of::<snow_draw_engine_core::PathChunk>()
        + geometry
            .path
            .chunks
            .iter()
            .map(|chunk| chunk.commands.len() * size_of::<PathCommand>())
            .sum::<usize>()
}

fn cached_geometry(value: &SpeechBubbleData) -> Arc<BubbleGeometry> {
    let key = GeometryKey::new(value);
    GEOMETRY_CACHE.with(|cache| {
        let mut cache = cache.borrow_mut();
        cache.clock = cache.clock.wrapping_add(1);
        let clock = cache.clock;
        if let Some(entry) = cache.entries.get_mut(&key) {
            entry.used = clock;
            return entry.geometry.clone();
        }
        let geometry = Arc::new(build_geometry(value, &key));
        cache.builds += 1;
        cache.retain(key, geometry.clone(), clock);
        geometry
    })
}

#[derive(Clone, Copy, Debug)]
struct Segment {
    start: Point<f64>,
    control: Option<[Point<f64>; 2]>,
    end: Point<f64>,
}

fn subtract(a: Point<f64>, b: Point<f64>) -> Point<f64> {
    Point::new(a.x - b.x, a.y - b.y)
}
fn cross(a: Point<f64>, b: Point<f64>) -> f64 {
    a.x * b.y - a.y * b.x
}
fn dot(a: Point<f64>, b: Point<f64>) -> f64 {
    a.x * b.x + a.y * b.y
}
fn mix(a: Point<f64>, b: Point<f64>, t: f64) -> Point<f64> {
    Point::new(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t)
}
fn line(start: Point<f64>, end: Point<f64>) -> Segment {
    Segment {
        start,
        control: None,
        end,
    }
}
fn cubic(start: Point<f64>, c1: Point<f64>, c2: Point<f64>, end: Point<f64>) -> Segment {
    Segment {
        start,
        control: Some([c1, c2]),
        end,
    }
}

impl Segment {
    fn tangent(self, t: f64) -> Point<f64> {
        if let Some([c1, c2]) = self.control {
            let a = subtract(c1, self.start);
            let b = subtract(c2, c1);
            let c = subtract(self.end, c2);
            mix(mix(a, b, t), mix(b, c, t), t)
        } else {
            subtract(self.end, self.start)
        }
    }
    fn eval(self, t: f64) -> Point<f64> {
        if let Some([c1, c2]) = self.control {
            let a = mix(self.start, c1, t);
            let b = mix(c1, c2, t);
            let c = mix(c2, self.end, t);
            mix(mix(a, b, t), mix(b, c, t), t)
        } else {
            mix(self.start, self.end, t)
        }
    }
    fn split(self, t: f64) -> (Self, Self) {
        if let Some([c1, c2]) = self.control {
            let a = mix(self.start, c1, t);
            let b = mix(c1, c2, t);
            let c = mix(c2, self.end, t);
            let d = mix(a, b, t);
            let e = mix(b, c, t);
            let middle = mix(d, e, t);
            (
                cubic(self.start, a, d, middle),
                cubic(middle, e, c, self.end),
            )
        } else {
            let middle = self.eval(t);
            (line(self.start, middle), line(middle, self.end))
        }
    }
    fn interval(self, a: f64, b: f64) -> Self {
        if a <= 0.0 {
            return self.split(b).0;
        }
        let after = self.split(a).1;
        after.split((b - a) / (1.0 - a)).0
    }
    fn command(self) -> PathCommand {
        let p = |p: Point<f64>| [p.x, p.y];
        match self.control {
            Some([c1, c2]) => PathCommand::CubicTo {
                control_1: p(c1),
                control_2: p(c2),
                end: p(self.end),
            },
            None => PathCommand::LineTo { point: p(self.end) },
        }
    }
}

fn arc_segments(center: Point<f64>, rx: f64, ry: f64, start: f64, sweep: f64) -> Vec<Segment> {
    let count = (sweep.abs() / std::f64::consts::FRAC_PI_2 - 1e-12)
        .ceil()
        .max(1.0) as usize;
    let step = sweep / count as f64;
    (0..count)
        .map(|i| {
            let a = start + step * i as f64;
            let b = a + step;
            let (sa, ca) = a.sin_cos();
            let (sb, cb) = b.sin_cos();
            let k = 4.0 / 3.0 * (step / 4.0).tan();
            cubic(
                Point::new(center.x + rx * ca, center.y + ry * sa),
                Point::new(center.x + rx * (ca - k * sa), center.y + ry * (sa + k * ca)),
                Point::new(center.x + rx * (cb + k * sb), center.y + ry * (sb - k * cb)),
                Point::new(center.x + rx * cb, center.y + ry * sb),
            )
        })
        .collect()
}

fn body_segments(body: RectangleData) -> Vec<Segment> {
    use std::f64::consts::{FRAC_PI_2, PI, TAU};
    let a = body.width / 2.0;
    let b = body.height / 2.0;
    if a <= 0.0 || b <= 0.0 {
        return Vec::new();
    }
    if body.highlight_shape == HighlightShape::ThoughtBubble {
        return thought_bubble::body_segments(body);
    }
    if body.highlight_shape == HighlightShape::Ellipse {
        return arc_segments(Point::new(0.0, 0.0), a, b, -FRAC_PI_2, TAU);
    }
    if body.highlight_shape == HighlightShape::Diamond {
        let commands = crate::RoundedDiamond::new(body.width, body.height, body.corner_radii)
            .path_commands(Point::new(0.0, 0.0), 0.0);
        let mut segments = Vec::new();
        let mut current = Point::new(0.0, 0.0);
        for command in commands {
            use snow_draw_engine_core::arrow::ArrowPathCommand;
            let p = |p: [f64; 2]| Point::new(p[0], p[1]);
            match command {
                ArrowPathCommand::MoveTo { point } => current = p(point),
                ArrowPathCommand::LineTo { point } => {
                    let end = p(point);
                    segments.push(line(current, end));
                    current = end;
                }
                ArrowPathCommand::CubicTo {
                    control_1,
                    control_2,
                    end,
                } => {
                    let end = p(end);
                    segments.push(cubic(current, p(control_1), p(control_2), end));
                    current = end;
                }
                ArrowPathCommand::QuadTo { .. } => unreachable!(),
            }
        }
        return segments;
    }
    let r = body.corner_radii;
    let mut segments = Vec::with_capacity(8);
    let entries = [
        Point::new(-a + r.top_left, -b),
        Point::new(a, -b + r.top_right),
        Point::new(a - r.bottom_right, b),
        Point::new(-a, b - r.bottom_left),
    ];
    let exits = [
        Point::new(a - r.top_right, -b),
        Point::new(a, b - r.bottom_right),
        Point::new(-a + r.bottom_left, b),
        Point::new(-a, -b + r.top_left),
    ];
    let radii = [r.top_right, r.bottom_right, r.bottom_left, r.top_left];
    let centers = [
        Point::new(a - r.top_right, -b + r.top_right),
        Point::new(a - r.bottom_right, b - r.bottom_right),
        Point::new(-a + r.bottom_left, b - r.bottom_left),
        Point::new(-a + r.top_left, -b + r.top_left),
    ];
    let angles = [-FRAC_PI_2, 0.0, FRAC_PI_2, PI];
    for i in 0..4 {
        segments.push(line(entries[i], exits[i]));
        if radii[i] > 0.0 {
            segments.extend(arc_segments(
                centers[i], radii[i], radii[i], angles[i], FRAC_PI_2,
            ));
        }
    }
    segments
}

/// Isolate cubic roots by derivative intervals, avoiding fragile cubic closed forms.
fn polynomial_roots(mut c: [f64; 4]) -> Vec<f64> {
    let scale = c.iter().map(|v| v.abs()).fold(0.0, f64::max);
    if scale == 0.0 || !scale.is_finite() {
        return Vec::new();
    }
    for value in &mut c {
        *value /= scale;
    }
    let eval = |t: f64| ((c[0] * t + c[1]) * t + c[2]) * t + c[3];
    let mut divisions = vec![0.0, 1.0];
    let qa = 3.0 * c[0];
    let qb = 2.0 * c[1];
    let qc = c[2];
    if qa.abs() <= 1e-14 {
        if qb.abs() > 1e-14 {
            divisions.push(-qc / qb);
        }
    } else {
        let discriminant = qb * qb - 4.0 * qa * qc;
        if discriminant >= 0.0 {
            let root = discriminant.sqrt();
            let q = -0.5 * (qb + root.copysign(qb));
            if q != 0.0 {
                divisions.push(q / qa);
                divisions.push(qc / q);
            } else {
                divisions.push(-qb / (2.0 * qa));
            }
        }
    }
    divisions.retain(|t| t.is_finite() && (0.0..=1.0).contains(t));
    divisions.sort_by(f64::total_cmp);
    divisions.dedup_by(|a, b| (*a - *b).abs() < 1e-12);
    let mut roots = Vec::new();
    for &t in &divisions {
        if eval(t).abs() < 1e-12 {
            roots.push(t);
        }
    }
    for pair in divisions.windows(2) {
        let mut a = pair[0];
        let mut b = pair[1];
        let mut fa = eval(a);
        if fa * eval(b) >= 0.0 {
            continue;
        }
        for _ in 0..48 {
            let mid = f64::midpoint(a, b);
            let fm = eval(mid);
            if fa * fm <= 0.0 {
                b = mid;
            } else {
                a = mid;
                fa = fm;
            }
        }
        roots.push(f64::midpoint(a, b));
    }
    roots.sort_by(f64::total_cmp);
    roots.dedup_by(|a, b| (*a - *b).abs() < 1e-10);
    roots
}

fn intersections(segment: Segment, from: Point<f64>, direction: Point<f64>) -> Vec<f64> {
    let coordinate_scale = [
        segment.start.x,
        segment.start.y,
        segment.end.x,
        segment.end.y,
        from.x,
        from.y,
        direction.x,
        direction.y,
    ]
    .into_iter()
    .map(f64::abs)
    .fold(1.0, f64::max);
    let direction = Point::new(
        direction.x / coordinate_scale,
        direction.y / coordinate_scale,
    );
    let value = |p: Point<f64>| {
        cross(
            Point::new(
                (p.x - from.x) / coordinate_scale,
                (p.y - from.y) / coordinate_scale,
            ),
            direction,
        )
    };
    let v0 = value(segment.start);
    let v3 = value(segment.end);
    if let Some([c1, c2]) = segment.control {
        let v1 = value(c1);
        let v2 = value(c2);
        polynomial_roots([
            -v0 + 3.0 * v1 - 3.0 * v2 + v3,
            3.0 * v0 - 6.0 * v1 + 3.0 * v2,
            -3.0 * v0 + 3.0 * v1,
            v0,
        ])
    } else if (v3 - v0).abs() > f64::MIN_POSITIVE {
        let t = -v0 / (v3 - v0);
        if (0.0..=1.0).contains(&t) {
            vec![t]
        } else {
            Vec::new()
        }
    } else {
        Vec::new()
    }
}

fn ray_radius(body: &[Segment], angle: f64) -> f64 {
    let (sin, cos) = angle.sin_cos();
    let direction = Point::new(cos, sin);
    body.iter()
        .flat_map(|segment| {
            intersections(*segment, Point::new(0.0, 0.0), direction)
                .into_iter()
                .map(|t| dot(segment.eval(t), direction))
        })
        .filter(|radius| *radius > 0.0)
        .fold(f64::INFINITY, f64::min)
}

fn point_inside_body(body: &[Segment], point: Point<f64>) -> bool {
    if body.is_empty() {
        return false;
    }
    let distance = point.x.hypot(point.y);
    if distance == 0.0 {
        return true;
    }
    let radius = ray_radius(body, point.y.atan2(point.x));
    distance < radius - 1e-9 * radius.max(1.0)
}

fn visible_base(body: &[Segment], angle: f64, tip: Point<f64>) -> bool {
    let (sin, cos) = angle.sin_cos();
    let ray = Point::new(cos, sin);
    let mut visible = false;
    let mut along_flat_face = false;
    for segment in body {
        for t in intersections(*segment, Point::new(0.0, 0.0), ray) {
            let point = segment.eval(t);
            if dot(point, ray) <= 0.0 {
                continue;
            }
            let tangent = segment.tangent(t);
            let scale = tangent.x.abs().max(tangent.y.abs());
            if scale == 0.0 {
                continue;
            }
            let tangent = Point::new(tangent.x / scale, tangent.y / scale);
            let outward = Point::new(tangent.y, -tangent.x);
            let to_tip = subtract(tip, point);
            let normal = dot(to_tip, outward);
            // A line along a flat face leaves the visible junction at the far
            // end of that face, not at this proposed attachment point.
            let forward = dot(to_tip, tangent);
            if segment.control.is_none()
                && normal.abs() <= 1e-12
                && ((t < 1.0 - 1e-10 && forward > 0.0) || (t > 1e-10 && forward < 0.0))
            {
                along_flat_face = true;
            }
            // Convexity guarantees an outgoing leg on the exterior side of
            // this supporting tangent never re-enters the body.
            visible |= normal >= -1e-12;
        }
    }
    visible && !along_flat_face
}

fn effective_half_angle(body: &[Segment], tail: &SpeechBubbleTail) -> f64 {
    let axis = tail.tip.y.atan2(tail.tip.x);
    let radius = ray_radius(body, axis);
    if tail.tip.x.hypot(tail.tip.y) <= radius + 1e-9 * radius.max(1.0) {
        return 0.0;
    }
    let visible = |spread: f64| {
        visible_base(body, axis - spread, tail.tip) && visible_base(body, axis + spread, tail.tip)
    };
    let mut low = 0.0;
    let mut high = SPEECH_BUBBLE_MAX_HALF_ANGLE;
    // Solve the chord length on the actual contour, rather than scaling an
    // angle by the center-to-border radius (which also varies along flat faces).
    for _ in 0..36 {
        let mid = f64::midpoint(low, high);
        if base_width_at_angle(body, axis, mid) < tail.base_width {
            low = mid;
        } else {
            high = mid;
        }
    }
    let requested = f64::midpoint(low, high);
    if visible(requested) {
        return requested;
    }
    low = 0.0;
    high = requested;
    for _ in 0..36 {
        let mid = f64::midpoint(low, high);
        if visible(mid) {
            low = mid;
        } else {
            high = mid;
        }
    }
    // Stay just off a tangency: a double intersection at an exact supporting
    // line magnifies floating-point error while adding no meaningful width.
    low * (1.0 - 1e-5)
}

fn tail_bases(body: &[Segment], tail: &SpeechBubbleTail) -> [Point<f64>; 2] {
    let axis = tail.tip.y.atan2(tail.tip.x);
    let spread = effective_half_angle(body, tail);
    bases_at_angle(body, axis, spread)
}

fn base_width_at_angle(body: &[Segment], axis: f64, spread: f64) -> f64 {
    let [left, right] = bases_at_angle(body, axis, spread);
    (left.x - right.x).hypot(left.y - right.y)
}

fn bases_at_angle(body: &[Segment], axis: f64, spread: f64) -> [Point<f64>; 2] {
    [-spread, spread].map(|offset| {
        let angle = axis + offset;
        let radius = ray_radius(body, angle);
        let (sin, cos) = angle.sin_cos();
        Point::new(radius * cos, radius * sin)
    })
}

fn triangle_contains(triangle: [Point<f64>; 3], point: Point<f64>, strict: bool) -> bool {
    let a = subtract(triangle[1], triangle[0]);
    let b = subtract(triangle[2], triangle[0]);
    let scale =
        a.x.abs()
            .max(a.y.abs())
            .max(b.x.abs())
            .max(b.y.abs())
            .max(1.0);
    if cross(
        Point::new(a.x / scale, a.y / scale),
        Point::new(b.x / scale, b.y / scale),
    )
    .abs()
        <= 1e-14
    {
        return false;
    }
    let mut positive = false;
    let mut negative = false;
    for i in 0..3 {
        let edge = subtract(triangle[(i + 1) % 3], triangle[i]);
        let delta = subtract(point, triangle[i]);
        let scale = edge
            .x
            .abs()
            .max(edge.y.abs())
            .max(delta.x.abs())
            .max(delta.y.abs())
            .max(1.0);
        let value = cross(
            Point::new(edge.x / scale, edge.y / scale),
            Point::new(delta.x / scale, delta.y / scale),
        );
        if strict && value.abs() < 1e-12 {
            return false;
        }
        positive |= value > 1e-12;
        negative |= value < -1e-12;
    }
    !(positive && negative)
}

fn triangle_covers_outward(
    triangle: [Point<f64>; 3],
    point: Point<f64>,
    outward: Point<f64>,
) -> bool {
    for i in 0..3 {
        let edge = subtract(triangle[(i + 1) % 3], triangle[i]);
        let delta = subtract(point, triangle[i]);
        let scale = edge
            .x
            .abs()
            .max(edge.y.abs())
            .max(delta.x.abs())
            .max(delta.y.abs())
            .max(1.0);
        let edge = Point::new(edge.x / scale, edge.y / scale);
        let value = cross(edge, Point::new(delta.x / scale, delta.y / scale));
        if value < -1e-12 || value.abs() <= 1e-12 && cross(edge, outward) <= 0.0 {
            return false;
        }
    }
    true
}

fn segment_cuts(segment: Segment, triangle: [Point<f64>; 3]) -> Vec<f64> {
    let mut cuts = Vec::new();
    for i in 0..3 {
        cuts.extend(intersections(
            segment,
            triangle[i],
            subtract(triangle[(i + 1) % 3], triangle[i]),
        ));
    }
    cuts
}

fn points_bounds(points: impl IntoIterator<Item = Point<f64>>) -> DrawRect {
    let mut bounds = DrawRect::new(
        f64::INFINITY,
        f64::INFINITY,
        f64::NEG_INFINITY,
        f64::NEG_INFINITY,
    );
    for point in points {
        bounds.min_x = bounds.min_x.min(point.x);
        bounds.min_y = bounds.min_y.min(point.y);
        bounds.max_x = bounds.max_x.max(point.x);
        bounds.max_y = bounds.max_y.max(point.y);
    }
    bounds
}

fn segment_hull_bounds(segment: Segment) -> DrawRect {
    points_bounds(
        [segment.start, segment.end]
            .into_iter()
            .chain(segment.control.into_iter().flatten()),
    )
}

fn bounds_overlap(a: DrawRect, b: DrawRect) -> bool {
    let epsilon = 1e-9;
    a.min_x <= b.max_x + epsilon
        && b.min_x <= a.max_x + epsilon
        && a.min_y <= b.max_y + epsilon
        && b.min_y <= a.max_y + epsilon
}

fn build_geometry(value: &SpeechBubbleData, key: &GeometryKey) -> BubbleGeometry {
    if value.body.highlight_shape == HighlightShape::ThoughtBubble {
        return thought_bubble::build_geometry(value, key);
    }
    let body = body_segments(speech_bubble_body_proxy(value));
    let triangles: Vec<_> = value
        .tails
        .iter()
        .map(|tail| {
            let [left, right] = tail_bases(&body, tail);
            [left, tail.tip, right]
        })
        .collect();
    let triangle_bounds: Vec<_> = triangles
        .iter()
        .map(|triangle| points_bounds(*triangle))
        .collect();
    let body_bounds: Vec<_> = body
        .iter()
        .map(|segment| segment_hull_bounds(*segment))
        .collect();
    let mut outline = Vec::new();
    let mut candidates: Vec<_> = body
        .iter()
        .copied()
        .map(|segment| (segment, None))
        .collect();
    for (i, triangle) in triangles.iter().enumerate() {
        candidates.push((line(triangle[0], triangle[1]), Some(i)));
        candidates.push((line(triangle[1], triangle[2]), Some(i)));
    }
    for (segment, owner) in candidates {
        if (segment.start.x - segment.end.x).hypot(segment.start.y - segment.end.y) < 1e-12 {
            continue;
        }
        let segment_bounds = segment_hull_bounds(segment);
        let mut cuts = vec![0.0, 1.0];
        for (i, triangle) in triangles.iter().enumerate() {
            if owner != Some(i) && bounds_overlap(segment_bounds, triangle_bounds[i]) {
                cuts.extend(segment_cuts(segment, *triangle));
            }
        }
        if owner.is_some() {
            for (edge, edge_bounds) in body.iter().zip(&body_bounds) {
                if !bounds_overlap(segment_bounds, *edge_bounds) {
                    continue;
                }
                for t in intersections(*edge, segment.start, subtract(segment.end, segment.start)) {
                    let p = edge.eval(t);
                    let delta = subtract(segment.end, segment.start);
                    let scale = delta.x.abs().max(delta.y.abs());
                    if scale > 0.0 {
                        let d = Point::new(delta.x / scale, delta.y / scale);
                        let u = dot(subtract(p, segment.start), d) / (scale * dot(d, d));
                        if (0.0..=1.0).contains(&u) {
                            cuts.push(u);
                        }
                    }
                }
            }
        }
        cuts.sort_by(f64::total_cmp);
        cuts.dedup_by(|a, b| (*a - *b).abs() < 1e-10);
        for pair in cuts.windows(2) {
            if pair[1] - pair[0] < 1e-10 {
                continue;
            }
            let middle = segment.eval(f64::midpoint(pair[0], pair[1]));
            if owner.is_some() {
                let radius = ray_radius(&body, middle.y.atan2(middle.x));
                if middle.x.hypot(middle.y) <= radius + 1e-9 * radius.max(1.0) {
                    continue;
                }
            }
            let tangent = subtract(
                segment.eval((f64::midpoint(pair[0], pair[1]) + 1e-6).min(1.0)),
                segment.eval((f64::midpoint(pair[0], pair[1]) - 1e-6).max(0.0)),
            );
            let outward = Point::new(tangent.y, -tangent.x);
            let hidden = triangles.iter().enumerate().any(|(i, triangle)| {
                owner != Some(i)
                    && middle.x >= triangle_bounds[i].min_x - 1e-9
                    && middle.x <= triangle_bounds[i].max_x + 1e-9
                    && middle.y >= triangle_bounds[i].min_y - 1e-9
                    && middle.y <= triangle_bounds[i].max_y + 1e-9
                    && (triangle_covers_outward(*triangle, middle, outward)
                        || owner.is_some_and(|j| value.tails[i].id < value.tails[j].id)
                            && triangle_contains(*triangle, middle, false))
            });
            if !hidden {
                outline.push(segment.interval(pair[0], pair[1]));
            }
        }
    }
    let angle = |p: Point<f64>| p.y.atan2(p.x).rem_euclid(std::f64::consts::TAU);
    outline.sort_by(|a, b| angle(a.start).total_cmp(&angle(b.start)));
    let mut commands = Vec::with_capacity(outline.len() + 2);
    if let Some(first) = outline.first() {
        commands.push(PathCommand::MoveTo {
            point: [first.start.x, first.start.y],
        });
        commands.extend(outline.iter().map(|segment| segment.command()));
        commands.push(PathCommand::LineTo {
            point: [first.start.x, first.start.y],
        });
    }
    let mut revision = std::collections::hash_map::DefaultHasher::new();
    key.hash(&mut revision);
    BubbleGeometry {
        body,
        tail_bases: Vec::new(),
        circles: Vec::new(),
        triangles,
        outline,
        path: PathGeometry::from_commands(revision.finish(), commands, true),
    }
}

fn segment_near_point(segment: Segment, point: Point<f64>, distance: f64, depth: u32) -> bool {
    let mut min_x = segment.start.x.min(segment.end.x);
    let mut max_x = segment.start.x.max(segment.end.x);
    let mut min_y = segment.start.y.min(segment.end.y);
    let mut max_y = segment.start.y.max(segment.end.y);
    if let Some(controls) = segment.control {
        for control in controls {
            min_x = min_x.min(control.x);
            max_x = max_x.max(control.x);
            min_y = min_y.min(control.y);
            max_y = max_y.max(control.y);
        }
    }
    if point.x < min_x - distance
        || point.x > max_x + distance
        || point.y < min_y - distance
        || point.y > max_y + distance
    {
        return false;
    }
    let line_distance = |p: Point<f64>| {
        let edge = subtract(segment.end, segment.start);
        let scale = edge.x.abs().max(edge.y.abs()).max(1.0);
        let edge = Point::new(edge.x / scale, edge.y / scale);
        let delta = subtract(p, segment.start);
        let length = dot(edge, edge);
        let t = if length > 0.0 {
            (dot(delta, edge) / (scale * length)).clamp(0.0, 1.0)
        } else {
            0.0
        };
        (delta.x - edge.x * scale * t).hypot(delta.y - edge.y * scale * t)
    };
    let flatness = segment.control.map_or(0.0, |controls| {
        controls.into_iter().map(line_distance).fold(0.0, f64::max)
    });
    let epsilon = (distance * 0.01).clamp(1e-7, 0.01);
    if flatness <= epsilon || depth >= 20 {
        return line_distance(point) <= distance + flatness;
    }
    let (left, right) = segment.split(0.5);
    segment_near_point(left, point, distance, depth + 1)
        || segment_near_point(right, point, distance, depth + 1)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{Document, ElementMeta, FillStyle, StrokeStyle, TextData, Transaction};
    use snow_draw_engine_core::ColorRgba8;

    fn bubble(shape: HighlightShape) -> SpeechBubbleData {
        SpeechBubbleData::new(RectangleData {
            text_element_id: None,
            stroke_rendering_style: Default::default(),
            stroke_rendering_seed: 0,
            rectangle_kind: RectangleElementKind::Rectangle,
            highlight_shape: shape,
            center: Point::new(0.0, 0.0),
            width: 100.0,
            height: 60.0,
            rotation: 0.0,
            fill: ColorRgba8 {
                r: 255,
                g: 255,
                b: 255,
                a: 255,
            },
            fill_style: FillStyle::Solid,
            stroke: ColorRgba8 {
                r: 0,
                g: 0,
                b: 0,
                a: 255,
            },
            stroke_width: 2.0,
            stroke_style: StrokeStyle::Solid,
            corner_radii: CornerRadii::splat(6.0),
            opacity: 1.0,
        })
    }

    #[test]
    fn clearing_speech_bubble_geometry_cache_releases_paths_and_allows_rebuild() {
        let rectangle = bubble(HighlightShape::Rectangle);
        let thought_bubble = bubble(HighlightShape::ThoughtBubble);
        let rectangle_path = speech_bubble_path_geometry(&rectangle);
        let thought_bubble_path = speech_bubble_path_geometry(&thought_bubble);
        assert!(speech_bubble_geometry_retained_bytes() > 0);
        let builds = speech_bubble_geometry_build_count();

        clear_speech_bubble_geometry_cache_for_current_thread();

        assert_eq!(speech_bubble_geometry_retained_bytes(), 0);
        assert_eq!(speech_bubble_path_geometry(&rectangle), rectangle_path);
        assert_eq!(
            speech_bubble_path_geometry(&thought_bubble),
            thought_bubble_path
        );
        assert_eq!(speech_bubble_geometry_build_count(), builds + 2);
    }

    #[test]
    fn speech_bubble_styled_bounds_and_hits_include_only_visible_ink_excursions() {
        use snow_draw_engine_core::arrow::StrokeRenderingStyle;

        for shape in [
            HighlightShape::Rectangle,
            HighlightShape::Ellipse,
            HighlightShape::Diamond,
            HighlightShape::ThoughtBubble,
        ] {
            let mut value = bubble(shape);
            value.body.rotation = 0.37;
            value.tails.push(speech_bubble_default_tail(
                &value,
                Point::new(0.0, 110.0),
                1.0,
            ));
            let plain = speech_bubble_bounds(&value);
            let path = speech_bubble_path_geometry(&value);
            for (style, extra) in [
                (StrokeRenderingStyle::Artist, 2.0),
                (StrokeRenderingStyle::Cartoonist, 4.0),
            ] {
                value.body.stroke_rendering_style = style;
                let styled = speech_bubble_bounds(&value);
                assert!((styled.min_x - (plain.min_x - extra)).abs() < 1e-9);
                assert!((styled.max_y - (plain.max_y + extra)).abs() < 1e-9);
                assert_eq!(speech_bubble_path_geometry(&value), path);
                value.body.stroke.a = 0;
                let hidden = speech_bubble_bounds(&value);
                assert!((hidden.min_x - path.canvas_bounds[0]).abs() < 1e-9);
                value.body.stroke.a = 255;
            }
        }

        let mut value = bubble(HighlightShape::Rectangle);
        value.body.fill.a = 0;
        value.body.rotation = 0.37;
        let point = speech_bubble_local_to_canvas(&value, Point::new(-52.5, 0.0));
        assert!(!speech_bubble_hit_test(&value, point, 0.0));
        value.body.stroke_rendering_style = StrokeRenderingStyle::Artist;
        assert!(speech_bubble_hit_test(&value, point, 0.0));
        value.body.stroke_rendering_style = StrokeRenderingStyle::Cartoonist;
        assert!(speech_bubble_hit_test(&value, point, 0.0));
        value.body.stroke.a = 0;
        assert!(!speech_bubble_hit_test(&value, point, 0.0));
        value.body.stroke.a = 255;
        value.body.stroke_width = 0.0;
        assert!(!speech_bubble_hit_test(&value, point, 0.0));
    }

    #[test]
    fn thought_bubble_cloud_preserves_bounds_and_radial_hit_testing_when_resized() {
        for (width, height) in [(280.0, 180.0), (160.0, 160.0), (20.0, 600.0), (800.0, 20.0)] {
            let mut value = bubble(HighlightShape::ThoughtBubble);
            value.body.width = width;
            value.body.height = height;
            value.body.stroke_width = 0.0;
            let geometry = cached_geometry(&value);
            let bounds = speech_bubble_bounds(&value);
            for (actual, expected) in [
                (bounds.min_x, -width / 2.0),
                (bounds.max_x, width / 2.0),
                (bounds.min_y, -height / 2.0),
                (bounds.max_y, height / 2.0),
            ] {
                assert!((actual - expected).abs() < 1e-9);
            }
            for (i, segment) in geometry.body.iter().enumerate() {
                let next = geometry.body[(i + 1) % geometry.body.len()];
                assert_eq!(
                    segment.end, next.start,
                    "the cloud contour closes without seams"
                );
                // Every point on a puff must be the first boundary encountered
                // from the center, including the inward scallops. Otherwise the
                // radial hit test would disagree with the painted cloud.
                for step in 0..=32 {
                    let point = segment.eval(f64::from(step) / 32.0);
                    let distance = point.x.hypot(point.y);
                    let boundary = ray_radius(&geometry.body, point.y.atan2(point.x));
                    assert!((boundary - distance).abs() < 1e-7);
                    assert!(speech_bubble_hit_test(
                        &value,
                        Point::new(point.x * 0.99, point.y * 0.99),
                        0.0
                    ));
                    assert!(!speech_bubble_hit_test(
                        &value,
                        Point::new(point.x * 1.01, point.y * 1.01),
                        0.0
                    ));
                }
            }
        }
    }

    #[test]
    fn thought_bubble_contours_trails_bounds_and_empty_gaps_agree() {
        for (width, height) in [(200.0, 120.0), (20.0, 600.0), (800.0, 20.0)] {
            for stroke in [0.0, 2.0, 12.0, 72.0] {
                let mut value = bubble(HighlightShape::ThoughtBubble);
                value.body.width = width;
                value.body.height = height;
                value.body.stroke_width = stroke;
                for i in 0..8 {
                    let angle = i as f64 * std::f64::consts::TAU / 8.0;
                    value.tails.push(SpeechBubbleTail {
                        id: i + 1,
                        tip: Point::new(
                            angle.cos() * (width + 240.0),
                            angle.sin() * (height + 240.0),
                        ),
                        base_width: 48.0 + stroke * 3.0,
                    });
                }
                assert_eq!(validate_speech_bubble(&value), Ok(()));
                assert_eq!(
                    crate::validate_rectangle(&value.body),
                    Err(ErrorCode::InvalidArgument)
                );
                assert!(!value.body.supports_corner_radius());
                let geometry = cached_geometry(&value);
                assert!(geometry.triangles.is_empty());
                assert!(geometry.circles.len() <= 3 * value.tails.len());
                assert_eq!(
                    geometry
                        .path
                        .flattened_commands()
                        .iter()
                        .filter(|command| { matches!(command, PathCommand::MoveTo { .. }) })
                        .count(),
                    1 + geometry.circles.len(),
                    "each trail circle remains disconnected from the cloud"
                );
                let bounds = speech_bubble_bounds(&value);
                for circle in &geometry.circles {
                    assert!(speech_bubble_hit_test(&value, circle.center, 0.0));
                    assert!(circle.center.x - circle.radius >= bounds.min_x);
                    assert!(circle.center.x + circle.radius <= bounds.max_x);
                    assert!(circle.center.y - circle.radius >= bounds.min_y);
                    assert!(circle.center.y + circle.radius <= bounds.max_y);
                }
                let saved = serde_json::to_string(&value).unwrap();
                let restored: SpeechBubbleData = serde_json::from_str(&saved).unwrap();
                assert_eq!(restored.body, value.body);
                assert_eq!(restored.tails.len(), value.tails.len());
                for (a, b) in restored.tails.iter().zip(&value.tails) {
                    assert_eq!((a.id, a.base_width), (b.id, b.base_width));
                    assert!((a.tip.x - b.tip.x).hypot(a.tip.y - b.tip.y) < 1e-9);
                }
            }
        }
        let mut value = bubble(HighlightShape::ThoughtBubble);
        value.tails.push(SpeechBubbleTail {
            id: 1,
            tip: Point::new(0.0, 220.0),
            base_width: 32.0,
        });
        let geometry = cached_geometry(&value);
        assert_eq!(
            geometry.circles.len(),
            3,
            "a long trail keeps three readable bubbles"
        );
        for circle in &geometry.circles {
            assert!(!geometry.body.iter().any(|segment| segment_near_point(
                *segment,
                circle.center,
                circle.radius + value.body.stroke_width,
                0
            )));
        }
        for pair in geometry.circles.windows(2) {
            assert!(pair[0].radius > pair[1].radius);
            let gap = Point::new(
                0.0,
                (pair[0].center.y + pair[0].radius + pair[1].center.y - pair[1].radius) / 2.0,
            );
            assert!(!speech_bubble_hit_test(&value, gap, 0.0));
        }
        assert_eq!(geometry.circles.last().unwrap().center, value.tails[0].tip);
    }

    #[test]
    fn thought_bubble_geometry_cache_ignores_placement_and_preserves_requested_style() {
        let mut value = bubble(HighlightShape::ThoughtBubble);
        value.tails.push(SpeechBubbleTail {
            id: 23,
            tip: Point::new(110.0, 180.0),
            base_width: 34.0,
        });
        let original = cached_geometry(&value);
        value.body.center = Point::new(300.0, -80.0);
        value.body.rotation = 0.7;
        value.body.fill.r = 19;
        value.body.corner_radii = CornerRadii::splat(400.0);
        assert!(Arc::ptr_eq(&original, &cached_geometry(&value)));
        for circle in &original.circles {
            assert!(speech_bubble_hit_test(
                &value,
                speech_bubble_local_to_canvas(&value, circle.center),
                0.0
            ));
        }
        value.body.stroke_width = 7.0;
        assert!(!Arc::ptr_eq(&original, &cached_geometry(&value)));
        assert_eq!(value.body.corner_radii, CornerRadii::splat(400.0));
        let base = speech_bubble_tail_base_points(&value, &value.tails[0]);
        for point in base {
            assert!(
                (speech_bubble_base_width_from_point(&value, &value.tails[0], point)
                    - value.tails[0].base_width)
                    .abs()
                    < 1e-9
            );
        }
        assert!(speech_bubble_geometry_retained_bytes() <= GEOMETRY_CACHE_BYTE_BUDGET);
    }

    #[test]
    fn thought_bubble_tiny_trails_are_finite_and_keep_their_endpoints() {
        let mut value = bubble(HighlightShape::ThoughtBubble);
        for distance in [0.00001, 0.1, 1.0, 4.0, 10.0, 30.0, 80.0, 300.0, 1e6] {
            value.tails = vec![SpeechBubbleTail {
                id: 1,
                tip: Point::new(0.0, value.body.height / 2.0 + distance),
                base_width: 32.0,
            }];
            let geometry = cached_geometry(&value);
            assert!(geometry.circles.len() <= 3);
            assert!(
                geometry
                    .circles
                    .iter()
                    .all(|c| c.radius.is_finite() && c.radius > 0.0)
            );
            if let Some(last) = geometry.circles.last() {
                assert_eq!(last.center, value.tails[0].tip);
            }
            assert_eq!(validate_speech_bubble(&value), Ok(()));
        }
        value.tails[0].tip = Point::new(0.0, value.body.height / 2.0 + 12.0);
        value.tails[0].base_width = 200.0;
        let short = cached_geometry(&value);
        assert_eq!(
            short.circles.len(),
            1,
            "a short trail compresses its requested spacing"
        );
        assert_eq!(short.circles[0].center, value.tails[0].tip);
        assert!(short.circles[0].radius < value.tails[0].base_width / 2.0);
        assert_eq!(value.tails[0].base_width, 200.0);
    }

    #[test]
    fn border_fill_keeps_stroke_hit_policy_without_invisible_tail_fill() {
        let mut value = bubble(HighlightShape::Rectangle);
        let tail = speech_bubble_default_tail(&value, Point::new(0.0, 100.0), 1.0);
        value.tails.push(tail);
        value.body.fill_style = FillStyle::Border;
        let mut transparent = value.clone();
        transparent.body.fill_style = FillStyle::Solid;
        transparent.body.fill.a = 0;
        for y in -40..110 {
            for x in -60..60 {
                let point = Point::new(f64::from(x), f64::from(y));
                assert_eq!(
                    speech_bubble_hit_test(&value, point, 1.0),
                    speech_bubble_hit_test(&transparent, point, 1.0)
                );
            }
        }
        value.body.stroke_width = 0.0;
        assert!(!speech_bubble_hit_test(&value, Point::default(), 0.0));
    }

    fn distance(a: Point<f64>, b: Point<f64>) -> f64 {
        (a.x - b.x).hypot(a.y - b.y)
    }

    #[test]
    fn speech_bubble_join_width_is_constant_across_directions_and_body_sizes() {
        for shape in [
            HighlightShape::Rectangle,
            HighlightShape::Ellipse,
            HighlightShape::Diamond,
        ] {
            let mut value = bubble(shape);
            value.body.width = 240.0;
            value.body.height = 120.0;
            let mut tail = speech_bubble_default_tail(&value, Point::new(0.0, 600.0), 1.0);
            for (width, height) in [(240.0, 120.0), (320.0, 180.0), (120.0, 240.0)] {
                value.body.width = width;
                value.body.height = height;
                for step in 0..72 {
                    let angle = step as f64 * std::f64::consts::TAU / 72.0;
                    tail.tip = Point::new(600.0 * angle.cos(), 600.0 * angle.sin());
                    let bases = speech_bubble_tail_base_points(&value, &tail);
                    let actual = distance(bases[0], bases[1]);
                    assert!(
                        (actual - 32.0).abs() < 1e-6,
                        "{shape:?} {width}x{height} at {angle}: join width {actual}"
                    );
                }
            }
        }
    }

    #[test]
    fn speech_bubble_symmetric_bases_follow_every_rounded_contour() {
        for shape in [
            HighlightShape::Rectangle,
            HighlightShape::Ellipse,
            HighlightShape::Diamond,
        ] {
            let value = bubble(shape);
            for angle in [0.0_f64, 0.25, 0.8, 1.57, 2.2, 3.2, 5.8] {
                let tail = SpeechBubbleTail {
                    id: 1,
                    tip: Point::new(120.0 * angle.cos(), 120.0 * angle.sin()),
                    base_width: 15.0,
                };
                for base in speech_bubble_tail_base_points(&value, &tail) {
                    assert!(!speech_bubble_body_contains(&value, base));
                    assert!(speech_bubble_body_contains(
                        &value,
                        Point::new(base.x * 0.999, base.y * 0.999)
                    ));
                    assert!(
                        (speech_bubble_base_width_from_point(&value, &tail, base)
                            - tail.base_width)
                            .abs()
                            < 1e-9
                    );
                }
            }
        }
    }

    #[test]
    fn speech_bubble_diamond_cap_preserves_half_size_text_rectangle() {
        for (width, height) in [(200.0, 40.0), (40.0, 200.0), (1000.0, 1.0), (100.0, 100.0)] {
            let mut value = bubble(HighlightShape::Diamond);
            value.body.width = width;
            value.body.height = height;
            value.body.corner_radii = CornerRadii::splat(width.min(height) / 2.0);
            assert!(
                speech_bubble_body_proxy(&value).corner_radii.top_left <= width.min(height) / 4.0
            );
            for sx in [-1.0, 1.0] {
                for sy in [-1.0, 1.0] {
                    assert!(speech_bubble_body_contains(
                        &value,
                        Point::new(width * 0.249999 * sx, height * 0.249999 * sy)
                    ));
                }
            }
        }
    }

    #[test]
    fn speech_bubble_union_omits_base_and_covered_edges() {
        let mut value = bubble(HighlightShape::Rectangle);
        value.body.corner_radii = CornerRadii::default();
        value.body.fill.a = 0;
        let tail = speech_bubble_default_tail(&value, Point::new(0.0, 90.0), 1.0);
        value.tails.push(tail);
        assert!(!speech_bubble_hit_test(&value, Point::new(0.0, 30.0), 0.0));
        assert!(speech_bubble_hit_test(&value, Point::new(0.0, 90.0), 0.0));
        let [left, _] = speech_bubble_tail_base_points(&value, &tail);
        assert!(speech_bubble_hit_test(
            &value,
            mix(left, tail.tip, 0.5),
            0.0
        ));
        value.tails.push(SpeechBubbleTail {
            id: 2,
            tip: Point::new(0.0, 60.0),
            base_width: tail.base_width,
        });
        assert!(!speech_bubble_hit_test(&value, Point::new(0.0, 60.0), 0.0));
        let before = speech_bubble_local_path_geometry(&value).flattened_commands();
        value.tails.push(SpeechBubbleTail { id: 3, ..tail });
        assert_eq!(
            before,
            speech_bubble_local_path_geometry(&value).flattened_commands()
        );
    }

    #[test]
    fn speech_bubble_union_is_continuous_with_overlapping_tails() {
        for shape in [
            HighlightShape::Rectangle,
            HighlightShape::Ellipse,
            HighlightShape::Diamond,
        ] {
            let mut value = bubble(shape);
            for (id, angle) in [0.0_f64, 0.5, 1.2, 2.5, 4.1, 5.5].into_iter().enumerate() {
                value.tails.push(SpeechBubbleTail {
                    id: id as u64 + 1,
                    tip: Point::new(angle.cos() * 90.0, angle.sin() * 90.0),
                    base_width: 20.0,
                });
            }
            let geometry = cached_geometry(&value);
            for (index, segment) in geometry.outline.iter().enumerate() {
                let next = geometry.outline[(index + 1) % geometry.outline.len()];
                assert!(
                    distance(segment.end, next.start) < 1e-6,
                    "{shape:?} outline gap: {:?} to {:?}",
                    segment.end,
                    next.start
                );
            }
            let bounds = speech_bubble_bounds(&value);
            for tail in &value.tails {
                assert!(bounds.min_x <= tail.tip.x && bounds.max_x >= tail.tip.x);
                assert!(bounds.min_y <= tail.tip.y && bounds.max_y >= tail.tip.y);
                assert!(speech_bubble_hit_test(&value, tail.tip, 0.1));
            }
        }
    }

    #[test]
    fn speech_bubble_growth_holds_top_and_canvas_tips_and_deletes_swallowed_tails() {
        let mut value = bubble(HighlightShape::Ellipse);
        value.body.center = Point::new(130.0, 70.0);
        value.body.rotation = 0.73;
        value.tails.push(speech_bubble_default_tail(
            &value,
            Point::new(0.0, 90.0),
            1.0,
        ));
        let top = speech_bubble_local_to_canvas(&value, Point::new(0.0, -30.0));
        let tip = speech_bubble_local_to_canvas(&value, value.tails[0].tip);
        let grown = speech_bubble_grow_height(&value, 100.0);
        assert!(
            distance(
                top,
                speech_bubble_local_to_canvas(&grown, Point::new(0.0, -50.0))
            ) < 1e-9
        );
        assert!(
            distance(
                tip,
                speech_bubble_local_to_canvas(&grown, grown.tails[0].tip)
            ) < 1e-9
        );
        assert_eq!(speech_bubble_grow_height(&grown, 20.0), grown);
        assert!(speech_bubble_grow_height(&grown, 240.0).tails.is_empty());
    }

    #[test]
    fn speech_bubble_serialization_requires_tails() {
        let value = bubble(HighlightShape::Rectangle);
        let mut serialized = serde_json::to_value(&value).unwrap();
        assert_eq!(
            serde_json::from_value::<SpeechBubbleData>(serialized.clone()).unwrap(),
            value
        );
        serialized.as_object_mut().unwrap().remove("tails");
        assert!(serde_json::from_value::<SpeechBubbleData>(serialized).is_err());
    }

    #[test]
    fn speech_bubble_legacy_angles_import_as_fixed_width_and_resave_without_angles() {
        for shape in [
            HighlightShape::Rectangle,
            HighlightShape::Ellipse,
            HighlightShape::Diamond,
        ] {
            let value = bubble(shape);
            let contour = body_segments(speech_bubble_body_proxy(&value));
            let mut saved = serde_json::to_value(&value).unwrap();
            saved["tails"] = serde_json::json!([{
                "id": 7,
                "tip": {"x": 0.0, "y": 300.0},
                "half_angle": 0.2,
            }]);
            let mut restored: SpeechBubbleData = serde_json::from_value(saved.clone()).unwrap();
            let expected = bases_at_angle(&contour, std::f64::consts::FRAC_PI_2, 0.2);
            let actual = speech_bubble_tail_base_points(&restored, &restored.tails[0]);
            for i in 0..2 {
                assert!(distance(actual[i], expected[i]) < 1e-8);
            }
            let width = distance(expected[0], expected[1]);
            restored.tails[0].tip = Point::new(300.0, 0.0);
            let actual = speech_bubble_tail_base_points(&restored, &restored.tails[0]);
            assert!((distance(actual[0], actual[1]) - width).abs() < 1e-8);
            let resaved = serde_json::to_value(&restored).unwrap();
            assert!(resaved["tails"][0].get("half_angle").is_none());
            assert_eq!(resaved["tails"][0]["base_width"], width);
            assert_eq!(
                serde_json::from_value::<SpeechBubbleData>(resaved).unwrap(),
                restored
            );
            for invalid in [-0.1, 0.0, 2.0] {
                saved["tails"][0]["half_angle"] = serde_json::json!(invalid);
                assert!(serde_json::from_value::<SpeechBubbleData>(saved.clone()).is_err());
            }
            saved["tails"][0]
                .as_object_mut()
                .unwrap()
                .remove("half_angle");
            assert!(serde_json::from_value::<SpeechBubbleData>(saved).is_err());
        }
    }

    #[test]
    fn speech_bubble_body_transforms_keep_all_canvas_tail_tips_fixed() {
        for shape in [
            HighlightShape::Rectangle,
            HighlightShape::Ellipse,
            HighlightShape::Diamond,
        ] {
            for rotation in [0.0, 0.57, -1.2] {
                let mut value = bubble(shape);
                value.body.rotation = rotation;
                for tip in [Point::new(-240.0, 210.0), Point::new(270.0, -220.0)] {
                    value
                        .tails
                        .push(speech_bubble_default_tail(&value, tip, 1.0));
                }
                let next = speech_bubble_with_selection_rect(
                    &value,
                    RectangleData {
                        center: Point::new(value.body.center.x + 25.0, value.body.center.y - 35.0),
                        width: value.body.width * 1.3,
                        height: value.body.height * 0.8,
                        rotation: rotation + 0.9,
                        ..value.body
                    },
                );
                assert_eq!(next.tails.len(), value.tails.len());
                for (before, after) in value.tails.iter().zip(&next.tails) {
                    let expected = speech_bubble_local_to_canvas(&value, before.tip);
                    let actual = speech_bubble_local_to_canvas(&next, after.tip);
                    assert!(
                        (expected.x - actual.x).hypot(expected.y - actual.y) < 1e-8,
                        "{shape:?}: {expected:?} moved to {actual:?}"
                    );
                    assert_eq!(before.id, after.id);
                    assert_eq!(before.base_width, after.base_width);
                }
                assert_eq!(validate_speech_bubble(&next), Ok(()));
            }
        }
    }

    #[test]
    fn speech_bubble_manual_resize_and_geometry_cache_preserve_expected_invariants() {
        let mut value = bubble(HighlightShape::Diamond);
        value.body.width = 117.3;
        value.tails.push(SpeechBubbleTail {
            id: 7,
            tip: Point::new(80.0, 90.0),
            base_width: 12.0,
        });
        let resized = speech_bubble_with_selection_rect(
            &value,
            RectangleData {
                width: 234.6,
                height: 30.0,
                rotation: 0.5,
                ..value.body
            },
        );
        let expected = speech_bubble_local_to_canvas(&value, value.tails[0].tip);
        let actual = speech_bubble_local_to_canvas(&resized, resized.tails[0].tip);
        assert!((expected.x - actual.x).hypot(expected.y - actual.y) < 1e-8);
        assert_eq!(resized.tails[0].base_width, 12.0);
        speech_bubble_path_geometry(&value);
        let builds = speech_bubble_geometry_build_count();
        value.body.center = Point::new(700.0, 400.0);
        value.body.rotation = 0.5;
        value.body.fill_style = FillStyle::CrossLine;
        value.body.fill.a = 130;
        value.body.stroke_width = 4.0;
        speech_bubble_path_geometry(&value);
        speech_bubble_hit_test(&value, value.body.center, 2.0);
        assert_eq!(builds, speech_bubble_geometry_build_count());
        value.tails[0].tip.y += 5.0;
        speech_bubble_path_geometry(&value);
        assert_eq!(builds + 1, speech_bubble_geometry_build_count());
        value.tails[0].base_width += 5.0;
        let bases = speech_bubble_tail_base_points(&value, &value.tails[0]);
        assert!((distance(bases[0], bases[1]) - 17.0).abs() < 1e-7);
        assert_eq!(builds + 2, speech_bubble_geometry_build_count());
    }

    #[test]
    fn speech_bubble_requested_radii_survive_small_body_validation_and_serialization() {
        for shape in [
            HighlightShape::Rectangle,
            HighlightShape::Ellipse,
            HighlightShape::Diamond,
        ] {
            let mut value = bubble(shape);
            value.body.width = 1.0;
            value.body.height = 2.0;
            value.body.corner_radii = CornerRadii::splat(80.0);
            assert_eq!(validate_speech_bubble(&value), Ok(()));
            let effective = speech_bubble_body_proxy(&value);
            assert_eq!(crate::validate_rectangle(&effective), Ok(()));
            assert!(effective.corner_radii.top_left <= 0.5);
            assert!(crate::validate_rectangle(&value.body).is_err());
            let json = serde_json::to_string(&value).unwrap();
            let restored: SpeechBubbleData = serde_json::from_str(&json).unwrap();
            assert_eq!(restored.body.corner_radii, CornerRadii::splat(80.0));
            for radius in [-1.0, f64::NAN, f64::INFINITY] {
                value.body.corner_radii.top_left = radius;
                assert_eq!(
                    validate_speech_bubble(&value),
                    Err(ErrorCode::InvalidArgument)
                );
            }
        }
    }

    #[test]
    fn speech_bubble_validation_rejects_invalid_tails_and_allows_contour_tip() {
        let mut value = bubble(HighlightShape::Rectangle);
        value.tails.push(SpeechBubbleTail {
            id: 1,
            tip: Point::new(0.0, 30.0),
            base_width: 12.0,
        });
        assert_eq!(validate_speech_bubble(&value), Ok(()));
        value.tails[0].tip.y = 29.0;
        assert_eq!(
            validate_speech_bubble(&value),
            Err(ErrorCode::InvalidArgument)
        );
        value.tails[0].tip.y = 70.0;
        value.tails.push(value.tails[0]);
        assert_eq!(
            validate_speech_bubble(&value),
            Err(ErrorCode::InvalidArgument)
        );
        value.tails.pop();
        value.tails[0].base_width = f64::NAN;
        assert_eq!(
            validate_speech_bubble(&value),
            Err(ErrorCode::InvalidArgument)
        );
    }

    #[test]
    fn speech_bubble_ownership_serialization_and_undo_are_atomic() {
        let mut document = Document::new();
        let label = document.allocate_element_id();
        let owner = document.allocate_element_id();
        let mut value = bubble(HighlightShape::Diamond);
        value.body.center = Point::new(100.0, 200.0);
        value.body.rotation = 0.2;
        value.text_element_id = Some(label);
        value.tails.push(speech_bubble_default_tail(
            &value,
            Point::new(30.0, 80.0),
            1.0,
        ));
        let mut transaction = Transaction::new("create bubble");
        transaction.insert_text(
            label,
            ElementMeta::default(),
            TextData {
                text: "label".to_owned(),
                ..TextData::default()
            },
        );
        transaction.insert_speech_bubble(owner, ElementMeta::default(), value.clone());
        let result = document.apply(&transaction).unwrap();
        assert_eq!(document.paint_order(), &[owner, label]);
        assert_eq!(document.text(label).unwrap().center, value.body.center);
        assert_eq!(document.text(label).unwrap().rotation, value.body.rotation);
        assert_eq!(document.text_owner_id(label), Some(owner));
        let saved = serde_json::to_string(&document).unwrap();
        let restored: Document = serde_json::from_str(&saved).unwrap();
        restored.validate_session().unwrap();
        assert_eq!(restored.speech_bubble(owner).unwrap(), &value);
        let other = document.allocate_element_id();
        let mut invalid = Transaction::new("shared label");
        invalid.insert_speech_bubble(other, ElementMeta::default(), value);
        let before = document.clone();
        assert!(document.apply(&invalid).is_err());
        assert_eq!(document, before);
        document
            .apply(&result.inverse)
            .unwrap_or_else(|error| panic!("{error:?} inverse {:?}", result.inverse));
        assert!(document.paint_order().is_empty());
    }

    #[test]
    fn speech_bubble_generated_silhouettes_remain_continuous_across_shapes_and_tail_overlaps() {
        let mut random = 123456789_u64;
        let mut sample = || {
            random ^= random << 13;
            random ^= random >> 7;
            random ^= random << 17;
            (random as f64) / (u64::MAX as f64)
        };
        for shape in [
            HighlightShape::Rectangle,
            HighlightShape::Ellipse,
            HighlightShape::Diamond,
        ] {
            for _ in 0..40 {
                let mut value = bubble(shape);
                value.body.width = 10.0 + sample() * 190.0;
                value.body.height = 10.0 + sample() * 190.0;
                value.body.corner_radii =
                    CornerRadii::splat(sample() * value.body.width.min(value.body.height) / 2.0);
                for id in 1..=8 {
                    let angle = sample() * std::f64::consts::TAU;
                    let radius = 300.0 + sample() * 200.0;
                    value.tails.push(SpeechBubbleTail {
                        id,
                        tip: Point::new(radius * angle.cos(), radius * angle.sin()),
                        base_width: 2.0 + sample() * 60.0,
                    });
                }
                let geometry = cached_geometry(&value);
                for (index, segment) in geometry.outline.iter().enumerate() {
                    let next = geometry.outline[(index + 1) % geometry.outline.len()];
                    assert!(
                        distance(segment.end, next.start) < 1e-5,
                        "{shape:?} gap {:?} to {:?}",
                        segment.end,
                        next.start
                    );
                }
                assert!(
                    geometry
                        .path
                        .canvas_bounds
                        .iter()
                        .all(|value| value.is_finite())
                );
            }
        }
    }

    #[test]
    fn speech_bubble_glyph_reserve_is_validated_serialized_and_included_in_text_bounds() {
        let mut text = TextData {
            text: "ink".to_owned(),
            text_ink_reserve: 7.0,
            ..TextData::default()
        };
        let reserve_bounds = crate::text_bounds(&text);
        assert!(crate::text_hit_test(
            &text,
            Point::new(text.width() / 2.0 + 6.5, 0.0),
            0.0
        ));
        let mut saved = serde_json::to_value(&text).unwrap();
        assert_eq!(
            serde_json::from_value::<TextData>(saved.clone()).unwrap(),
            text
        );
        saved.as_object_mut().unwrap().remove("text_ink_reserve");
        assert!(serde_json::from_value::<TextData>(saved).is_err());
        text.text_ink_reserve = 0.0;
        let ordinary_bounds = crate::text_bounds(&text);
        assert!((reserve_bounds.min_x - ordinary_bounds.min_x + 7.0).abs() < 1e-9);
        assert!((reserve_bounds.max_y - ordinary_bounds.max_y - 7.0).abs() < 1e-9);
        text.text_ink_reserve = -1.0;
        assert_eq!(crate::validate_text(&text), Err(ErrorCode::InvalidArgument));
        text.text_ink_reserve = f64::NAN;
        assert_eq!(crate::validate_text(&text), Err(ErrorCode::InvalidArgument));
    }

    #[test]
    fn speech_bubble_size_growth_preserves_rotated_top_left_and_canvas_tail_tips() {
        let mut value = bubble(HighlightShape::Rectangle);
        value.body.center = Point::new(100.0, 200.0);
        value.body.rotation = 1.1;
        value.tails.push(SpeechBubbleTail {
            id: 1,
            tip: Point::new(-100.0, -100.0),
            base_width: 12.0,
        });
        let corner = speech_bubble_local_to_canvas(&value, Point::new(-50.0, -30.0));
        let tip = speech_bubble_local_to_canvas(&value, value.tails[0].tip);
        let grown = speech_bubble_grow_size(&value, 180.0, 100.0);
        assert!(
            distance(
                corner,
                speech_bubble_local_to_canvas(&grown, Point::new(-90.0, -50.0))
            ) < 1e-9
        );
        assert!(
            distance(
                tip,
                speech_bubble_local_to_canvas(&grown, grown.tails[0].tip)
            ) < 1e-9
        );
        assert_eq!(speech_bubble_grow_size(&grown, 120.0, 80.0), grown);
    }

    #[test]
    fn speech_bubble_geometry_cache_evicts_by_bytes_and_does_not_retain_oversized_entries() {
        let mut cache = GeometryCache::default();
        let mut value = bubble(HighlightShape::Rectangle);
        for index in 0..12 {
            value.body.width = 100.0 + index as f64;
            let key = GeometryKey::new(&value);
            let geometry = Arc::new(BubbleGeometry {
                body: vec![line(Point::new(0.0, 0.0), Point::new(1.0, 1.0)); 10_000],
                triangles: Vec::new(),
                tail_bases: Vec::new(),
                circles: Vec::new(),
                outline: Vec::new(),
                path: PathGeometry::default(),
            });
            cache.retain(key, geometry, index);
            assert!(cache.retained_bytes <= GEOMETRY_CACHE_BYTE_BUDGET);
        }
        assert!(cache.entries.len() < 12);
        let bytes = cache.retained_bytes;
        let entries = cache.entries.len();
        value.body.width = 999.0;
        let oversized = Arc::new(BubbleGeometry {
            body: vec![
                line(Point::new(0.0, 0.0), Point::new(1.0, 1.0));
                GEOMETRY_CACHE_BYTE_BUDGET / std::mem::size_of::<Segment>() + 1
            ],
            triangles: Vec::new(),
            tail_bases: Vec::new(),
            circles: Vec::new(),
            outline: Vec::new(),
            path: PathGeometry::default(),
        });
        cache.retain(GeometryKey::new(&value), oversized, 100);
        assert_eq!(cache.retained_bytes, bytes);
        assert_eq!(cache.entries.len(), entries);
    }

    #[test]
    fn speech_bubble_transparent_body_picks_interior_without_changing_shape_picking() {
        let mut value = bubble(HighlightShape::Ellipse);
        value.body.fill.a = 0;
        assert!(speech_bubble_hit_test(&value, Point::new(0.0, 0.0), 0.0));
        assert!(!crate::rectangle_hit_test(
            &value.body,
            Point::new(0.0, 0.0),
            0.0
        ));
        assert!(!speech_bubble_hit_test(&value, Point::new(48.0, 28.0), 0.0));
        value.tails.push(SpeechBubbleTail {
            id: 1,
            tip: Point::new(80.0, 90.0),
            base_width: 12.0,
        });
        assert!(speech_bubble_hit_test(&value, Point::new(80.0, 90.0), 0.0));
        value.body.stroke.a = 0;
        assert!(!speech_bubble_hit_test(&value, Point::new(0.0, 0.0), 0.0));
        assert!(!speech_bubble_hit_test(&value, Point::new(80.0, 90.0), 0.0));
    }

    #[test]
    fn speech_bubble_effective_base_spread_stays_on_exterior_junctions_near_body() {
        for (shape, width, height, tip) in [
            (HighlightShape::Ellipse, 100.0, 60.0, Point::new(0.0, 31.0)),
            (
                HighlightShape::Rectangle,
                200.0,
                10.0,
                Point::new(110.0, 1.0),
            ),
            (HighlightShape::Diamond, 200.0, 10.0, Point::new(85.0, 2.0)),
        ] {
            let mut value = bubble(shape);
            value.body.width = width;
            value.body.height = height;
            value.body.corner_radii = CornerRadii::splat(1.0);
            let tail = SpeechBubbleTail {
                id: 1,
                tip,
                base_width: 60.0,
            };
            value.tails.push(tail);
            let effective = speech_bubble_tail_effective_half_angle(&value, &tail);
            assert!(
                effective > 0.0
                    && distance(
                        speech_bubble_tail_base_points(&value, &tail)[0],
                        speech_bubble_tail_base_points(&value, &tail)[1],
                    ) < tail.base_width,
                "{shape:?} effective {effective}"
            );
            for base in speech_bubble_tail_base_points(&value, &tail) {
                assert!(!speech_bubble_body_contains(&value, base));
                for t in [0.001, 0.01, 0.1, 0.5, 0.95] {
                    assert!(
                        !speech_bubble_body_contains(&value, mix(base, tip, t)),
                        "{shape:?} leg enters body at {t}"
                    );
                }
            }
            assert_eq!(value.tails[0].base_width, 60.0);
            let builds = speech_bubble_geometry_build_count();
            for _ in 0..5 {
                speech_bubble_tail_base_points(&value, &tail);
                speech_bubble_local_path_geometry(&value);
            }
            assert_eq!(builds, speech_bubble_geometry_build_count());
        }
    }

    #[test]
    fn speech_bubble_zero_length_tail_retains_requested_spread_without_phantom_fill() {
        let mut value = bubble(HighlightShape::Ellipse);
        value.tails.push(SpeechBubbleTail {
            id: 1,
            tip: Point::new(0.0, 30.0),
            base_width: 42.0,
        });
        assert_eq!(validate_speech_bubble(&value), Ok(()));
        assert!(speech_bubble_tail_effective_half_angle(&value, &value.tails[0]) < 1e-12);
        let bases = speech_bubble_tail_base_points(&value, &value.tails[0]);
        assert!(distance(bases[0], bases[1]) < 1e-12);
        assert!(!speech_bubble_hit_test(
            &value,
            Point::new(200.0, 200.0),
            0.0
        ));
        assert_eq!(value.tails[0].base_width, 42.0);
        value.tails[0].tip.y = 60.0;
        assert!(speech_bubble_tail_effective_half_angle(&value, &value.tails[0]) > 0.1);
    }
}
