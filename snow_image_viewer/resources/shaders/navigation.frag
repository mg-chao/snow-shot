#version 440

layout(location = 0) in vec4 vertexColor;
layout(location = 1) in vec4 vertexShape;
layout(location = 0) out vec4 fragmentColor;

const float kControlRadius = 21.0;
const float kComparisonThumbRadius = 24.0;
const float kComparisonArrowHalfHeight = 8.0;
const float kComparisonArrowWidth = 7.0;
const float kArrowHalfHeight = 8.0;
const float kArrowHalfWidth = 5.0;
const float kArrowHalfStroke = 0.8;
const float kAntialiasPixels = 0.75;

float distanceToSegment(vec2 point, vec2 start, vec2 end)
{
    vec2 segment = end - start;
    float projection = clamp(dot(point - start, segment) / dot(segment, segment), 0.0, 1.0);
    return length(point - (start + projection * segment));
}

float signedDistanceToTriangle(vec2 point, vec2 tip, vec2 upperCorner, vec2 lowerCorner)
{
    vec2 edge0 = upperCorner - tip;
    vec2 edge1 = lowerCorner - upperCorner;
    vec2 edge2 = tip - lowerCorner;
    vec2 offset0 = point - tip;
    vec2 offset1 = point - upperCorner;
    vec2 offset2 = point - lowerCorner;
    vec2 nearest0 = offset0 - edge0 * clamp(dot(offset0, edge0) / dot(edge0, edge0), 0.0, 1.0);
    vec2 nearest1 = offset1 - edge1 * clamp(dot(offset1, edge1) / dot(edge1, edge1), 0.0, 1.0);
    vec2 nearest2 = offset2 - edge2 * clamp(dot(offset2, edge2) / dot(edge2, edge2), 0.0, 1.0);
    float orientation = sign(edge0.x * edge2.y - edge0.y * edge2.x);
    vec2 distanceAndSide = min(
        min(vec2(dot(nearest0, nearest0),
                 orientation * (offset0.x * edge0.y - offset0.y * edge0.x)),
            vec2(dot(nearest1, nearest1),
                 orientation * (offset1.x * edge1.y - offset1.y * edge1.x))),
        vec2(dot(nearest2, nearest2),
             orientation * (offset2.x * edge2.y - offset2.y * edge2.x)));
    return -sqrt(distanceAndSide.x) * sign(distanceAndSide.y);
}

void main()
{
    float antialiasWidth = kAntialiasPixels / max(vertexShape.z, 1.0);
    float coverage;
    float shapeMode = abs(vertexShape.w);
    if (shapeMode < 0.5) {
        coverage = 1.0 - smoothstep(kControlRadius - antialiasWidth,
                                    kControlRadius + antialiasWidth,
                                    length(vertexShape.xy));
    } else if (shapeMode < 1.5) {
        float direction = vertexShape.w;
        vec2 tip = vec2(direction * kArrowHalfWidth, 0.0);
        vec2 upperCorner = vec2(-direction * kArrowHalfWidth, -kArrowHalfHeight);
        vec2 lowerCorner = vec2(-direction * kArrowHalfWidth, kArrowHalfHeight);
        float distance = min(distanceToSegment(vertexShape.xy, tip, upperCorner),
                             distanceToSegment(vertexShape.xy, tip, lowerCorner));
        coverage = 1.0 - smoothstep(kArrowHalfStroke - antialiasWidth,
                                    kArrowHalfStroke + antialiasWidth, distance);
    } else if (shapeMode < 2.5) {
        coverage = 1.0 - smoothstep(kComparisonThumbRadius - antialiasWidth,
                                    kComparisonThumbRadius + antialiasWidth,
                                    length(vertexShape.xy));
    } else if (shapeMode < 3.5) {
        coverage = 1.0;
    } else {
        float direction = shapeMode < 4.5 ? -1.0 : 1.0;
        vec2 tip = vec2(direction * kComparisonArrowWidth, 0.0);
        vec2 upperCorner = vec2(0.0, -kComparisonArrowHalfHeight);
        vec2 lowerCorner = vec2(0.0, kComparisonArrowHalfHeight);
        float distance = signedDistanceToTriangle(vertexShape.xy, tip, upperCorner,
                                                  lowerCorner);
        coverage = 1.0 - smoothstep(-antialiasWidth, antialiasWidth, distance);
    }
    fragmentColor = vec4(vertexColor.rgb, vertexColor.a * coverage);
}
