#include "balls_localizer.h"
#include "geometry.h"
#include "segmentation.h"
#include "masking.h"

#include <opencv2/features2d.hpp>

#include <iostream>
#include <cmath>
#include <map>
#include <queue>
#include <cassert>

using namespace cv;
using namespace std;

void extract_seed_points(const Mat &inrange_segmentation_mask, vector<Point> &seed_points)
{
    // Ensure the seed_points vector is empty
    seed_points.clear();

    // Loop through each pixel in the mask
    for (int y = 0; y < inrange_segmentation_mask.rows; ++y)
    {
        for (int x = 0; x < inrange_segmentation_mask.cols; ++x)
        {
            // Check if the pixel value is non-zero
            if (inrange_segmentation_mask.at<uchar>(y, x) > 0)
            {
                // Add the point to the vector
                seed_points.push_back(Point(x, y));
            }
        }
    }
}

void fill_small_holes(cv::Mat &binary_mask, double area_threshold)
{
    CV_Assert(binary_mask.type() == CV_8UC1);

    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(binary_mask, contours, hierarchy, cv::RETR_CCOMP, cv::CHAIN_APPROX_SIMPLE);

    for (int i = 0; i < contours.size(); ++i)
    {
        double area = cv::contourArea(contours[i]);

        // If the area is less than the threshold, fill the hole
        if (area < area_threshold)
        {
            cv::drawContours(binary_mask, contours, i, cv::Scalar(255), cv::FILLED, 8, hierarchy, 1);
        }
    }
}

void extract_bounding_boxes(const std::vector<cv::Vec3f> &circles, std::vector<cv::Rect> &bounding_boxes)
{
    for (const auto &circle : circles)
    {
        // Extract center coordinates and radius from the circle
        int centerX = static_cast<int>(circle[0]);
        int centerY = static_cast<int>(circle[1]);
        int radius = static_cast<int>(circle[2]);

        // Calculate top-left corner of the bounding box
        int x = centerX - radius;
        int y = centerY - radius;

        // Calculate width and height of the bounding box
        int width = 2 * radius;
        int height = 2 * radius;

        // Create a cv::Rect object
        cv::Rect roi(x, y, width, height);

        // Add the bounding box to the vector
        bounding_boxes.push_back(roi);
    }
}

void balls_localizer::localize(const Mat &src)
{
    const int FILTER_SIZE = 3;
    const int FILTER_SIGMA = 3;
    Mat blurred;
    GaussianBlur(src, blurred, Size(FILTER_SIZE, FILTER_SIZE), FILTER_SIGMA, FILTER_SIGMA);

    // Mat mask_bgr;
    // cvtColor(playing_field_mask, mask_bgr, COLOR_GRAY2BGR);
    // bitwise_and(masked, mask_bgr, masked);

    Mat masked;
    mask_bgr(blurred, masked, playing_field_mask);

    Mat masked_hsv;
    cvtColor(masked, masked_hsv, COLOR_BGR2HSV);

    vector<Point> seed_points;
    Mat inrange_segmentation_mask_board, inrange_segmentation_mask_shadows, segmentation_mask;

    int RADIUS = 100;
    Vec3b board_color_hsv = get_board_color(masked_hsv, RADIUS);
    const Vec3b SHADOW_OFFSET = Vec3b(0, 0, 90);
    Vec3b shadow_hsv = board_color_hsv - SHADOW_OFFSET;
    inRange(masked_hsv, board_color_hsv - Vec3b(4, 50, 40), board_color_hsv + Vec3b(4, 40, 15), inrange_segmentation_mask_board);
    inRange(masked_hsv, shadow_hsv - Vec3b(7, 100, 100), shadow_hsv + Vec3b(7, 80, 80), inrange_segmentation_mask_shadows);
    morphologyEx(inrange_segmentation_mask_shadows.clone(), inrange_segmentation_mask_shadows, MORPH_OPEN, getStructuringElement(MORPH_CROSS, Size(3, 3)));

    Mat outer_field;
    Mat shrinked_playing_field_mask;
    erode(playing_field_mask, shrinked_playing_field_mask, getStructuringElement(MORPH_CROSS, Size(70, 70)));
    bitwise_not(shrinked_playing_field_mask, outer_field);
    imshow("outer_field", outer_field);

    // Consider shadow segmentation mask only near the table edges
    bitwise_and(inrange_segmentation_mask_shadows.clone(), outer_field, inrange_segmentation_mask_shadows);

    imshow("inrange_sementation_1", inrange_segmentation_mask_board);
    imshow("inrange_sementation_2", inrange_segmentation_mask_shadows);
    bitwise_or(inrange_segmentation_mask_board, inrange_segmentation_mask_shadows, segmentation_mask);

    extract_seed_points(segmentation_mask, seed_points);
    region_growing(masked_hsv, segmentation_mask, seed_points, 3, 6, 4);

    vector<Point> mask_seed_points;
    Mat out_of_field_mask;
    mask_seed_points.push_back(Point(0, 0));
    mask_region_growing(segmentation_mask, out_of_field_mask, mask_seed_points);

    bitwise_or(segmentation_mask.clone(), out_of_field_mask, segmentation_mask);
    fill_small_holes(segmentation_mask, 90);
    imshow("segmentation", segmentation_mask);

    Mat display_segm, inrange_segmentation_mask_bgr;
    cvtColor(inrange_segmentation_mask_board, inrange_segmentation_mask_bgr, COLOR_GRAY2BGR);
    bitwise_and(masked, inrange_segmentation_mask_bgr, display_segm);

    const int HOUGH_MIN_RADIUS = 8;
    const int HOUGH_MAX_RADIUS = 16;
    const float HOUGH_DP = 0.3;
    const int HOUGH_MIN_DISTANCE = 18;
    vector<Vec3f> circles;
    HoughCircles(segmentation_mask, circles, HOUGH_GRADIENT, HOUGH_DP, HOUGH_MIN_DISTANCE, 100, 5, HOUGH_MIN_RADIUS, HOUGH_MAX_RADIUS);

    vector<Mat> hough_circle_masks;
    circles_masks(circles, hough_circle_masks, src.size());
    filter_empty_circles(circles, hough_circle_masks, segmentation_mask, 0.60);
    filter_out_of_bound_circles(circles, playing_field_mask, 20);
    filter_near_holes_circles(circles, playing_field_hole_points, 27);

    Mat display = blurred.clone();
    for (size_t i = 0; i < circles.size(); i++)
    {
        Vec3i c = circles[i];
        Point center = Point(c[0], c[1]);
        circle(display, center, 1, Scalar(0, 100, 100), 2, LINE_AA);
        int radius = c[2];
        circle(display, center, radius, Scalar(255, 0, 255), 2, LINE_AA);
    }
    imshow("", display);
    // waitKey(0);

    extract_bounding_boxes(circles, rois);
}

void balls_localizer::circles_masks(const std::vector<Vec3f> &circles, std::vector<Mat> &masks, Size mask_size)
{
    for (size_t i = 0; i < circles.size(); i++)
    {
        Mat mask(mask_size, CV_8U);
        mask.setTo(Scalar(0));
        circle(mask, Point(circles[i][0], circles[i][1]), circles[i][2], Scalar(255), FILLED);
        masks.push_back(mask);
    }
}

void balls_localizer::filter_empty_circles(std::vector<cv::Vec3f> &circles, const std::vector<Mat> &masks, const Mat &segmentation_mask, float intersection_threshold)
{
    std::vector<cv::Vec3f> filtered_circles;
    for (size_t i = 0; i < circles.size(); i++)
    {
        float circle_area = countNonZero(masks[i]);
        Mat masks_intersection;
        bitwise_and(masks[i], segmentation_mask, masks_intersection);
        float intersection_area = countNonZero(masks_intersection);

        if (intersection_area / circle_area < intersection_threshold)
        {
            filtered_circles.push_back(circles[i]);
        }
    }
    circles = filtered_circles;
}

void balls_localizer::filter_out_of_bound_circles(std::vector<cv::Vec3f> &circles, const Mat &table_mask, int distance_threshold)
{
    std::vector<cv::Vec3f> filtered_circles;
    Mat shrinked_table_mask;
    erode(table_mask, shrinked_table_mask, getStructuringElement(MORPH_CROSS, Size(distance_threshold, distance_threshold)));

    for (Vec3f circle : circles)
    {
        Point center = Point(circle[0], circle[1]);
        if (shrinked_table_mask.at<uchar>(center) == 255)
        {
            filtered_circles.push_back(circle);
        }
    }
    circles = filtered_circles;
}

void balls_localizer::filter_near_holes_circles(std::vector<cv::Vec3f> &circles, const vector<Point> &holes_points, float distance_threshold)
{
    std::vector<cv::Vec3f> filtered_circles;
    for (Vec3f circle : circles)
    {
        Point circle_point = Point(static_cast<int>(circle[0]), static_cast<int>(circle[1]));
        bool keep_circle = true;
        for (Point hole_point : holes_points)
        {
            if (norm(hole_point - circle_point) < distance_threshold)
                keep_circle = false;
        }

        if (keep_circle)
            filtered_circles.push_back(circle);
    }
    circles = filtered_circles;
}

void balls_localizer::segmentation(const Mat &src, Mat &dst)
{
    // HSV allows to separate brightness from other color characteristics, therefore
    // it is employed for kmeans clustering.
    cvtColor(src, dst, COLOR_BGR2HSV);

    const int VALUE_UNIFORM = 255;
    vector<Mat> hsv_channels;
    split(dst, hsv_channels);
    hsv_channels[1].setTo(VALUE_UNIFORM);
    hsv_channels[2].setTo(VALUE_UNIFORM);
    merge(hsv_channels, dst);

    const int NUMBER_OF_CENTERS = 8;
    kmeans(src, dst, NUMBER_OF_CENTERS);
}

Vec3b balls_localizer::get_board_color(const Mat &src, float radius)
{
    int center_cols = src.cols / 2;
    int center_rows = src.rows / 2;
    vector<Vec3b> pixel_values;

    // Collect all pixel values in a radius 'radius' around the image center.
    for (int row = -radius; row <= radius; ++row)
    {
        for (int col = -radius; col <= radius; ++col)
        {
            if (col * col + row * row <= radius * radius)
            {
                int current_row = center_rows + row;
                int current_col = center_cols + col;

                if (current_row >= 0 && current_row < src.rows && current_col >= 0 && current_col < src.cols)
                {
                    pixel_values.push_back(src.at<Vec3b>(current_row, current_col));
                }
            }
        }
    }

    // Return black if no pixel_values are collected
    if (pixel_values.empty())
    {
        return Vec3b(0, 0, 0);
    }

    // Sort by norm. In a grayscale context, we would have just considered the pixel intensity.
    // However, now we have 3 components. So we sort the pixel values triplets by their norm.
    sort(pixel_values.begin(), pixel_values.end(), [](const Vec3b &a, const Vec3b &b)
         { return norm(a) < norm(b); });

    return pixel_values[pixel_values.size() / 2];
}
