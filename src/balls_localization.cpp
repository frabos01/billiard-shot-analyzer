#include "balls_localization.h"
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

bool operator==(const ball_localization &lhs, const ball_localization &rhs)
{
    return lhs.circle == rhs.circle && lhs.bounding_box == rhs.bounding_box;
}

bool operator!=(const ball_localization &lhs, const ball_localization &rhs)
{
    return !(lhs == rhs);
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
    mask_bgr(blurred, masked, playing_field.mask);

    Mat masked_hsv;
    cvtColor(masked, masked_hsv, COLOR_BGR2HSV);

    vector<Mat> channels;
    split(masked_hsv, channels);
    imshow("0", channels[0]);
    imshow("1", channels[1]);
    imshow("2", channels[2]);

    vector<Point> seed_points;
    Mat board_mask;
    Mat shadows_mask;
    Mat color_mask;
    Mat final_segmentation_mask;

    int RADIUS = 100;
    const Vec3b board_color_hsv = get_board_color(masked_hsv, RADIUS);
    const Vec3b SHADOW_OFFSET = Vec3b(0, 0, 90);

    Vec3b shadow_hsv = board_color_hsv - SHADOW_OFFSET;
    inRange(masked_hsv, board_color_hsv - Vec3b(5, 80, 50), board_color_hsv + Vec3b(5, 60, 15), board_mask);
    inRange(masked_hsv, shadow_hsv - Vec3b(3, 30, 80), shadow_hsv + Vec3b(3, 100, 40), shadows_mask);
    inRange(masked_hsv, board_color_hsv - Vec3b(10, 255, 150), shadow_hsv + Vec3b(10, 255, 255), color_mask);

    Mat outer_field;
    Mat shrinked_playing_field_mask;

    // Consider shadow mask only near the table edges
    const int DEPTH_SHADOW_MASK = 50;
    erode(playing_field.mask, shrinked_playing_field_mask, getStructuringElement(MORPH_CROSS, Size(DEPTH_SHADOW_MASK, DEPTH_SHADOW_MASK)));
    bitwise_not(shrinked_playing_field_mask, outer_field);
    bitwise_and(shadows_mask.clone(), outer_field, shadows_mask);

    // Consider color mask only near the table edges
    const int DEPTH_COLOR_MASK = 30;
    erode(playing_field.mask, shrinked_playing_field_mask, getStructuringElement(MORPH_CROSS, Size(DEPTH_COLOR_MASK, DEPTH_COLOR_MASK)));
    bitwise_not(shrinked_playing_field_mask, outer_field);
    bitwise_and(color_mask.clone(), outer_field, color_mask);

    // imshow("inrange_sementation_1", inrange_segmentation_mask_board);
    // imshow("inrange_sementation_2", inrange_segmentation_mask_shadows);
    // Union of the above masks
    bitwise_or(board_mask, shadows_mask, final_segmentation_mask);
    bitwise_or(final_segmentation_mask, color_mask, final_segmentation_mask);

    extract_seed_points(final_segmentation_mask, seed_points);
    region_growing(masked_hsv, final_segmentation_mask, seed_points, 3, 6, 4);

    // imshow("segmentation_before", segmentation_mask);

    const Size CLOSURE_SIZE = Size(3, 3);
    morphologyEx(final_segmentation_mask.clone(), final_segmentation_mask, MORPH_CLOSE, getStructuringElement(MORPH_ELLIPSE, CLOSURE_SIZE));

    const int AREA_THRESHOLD = 90;
    fill_small_holes(final_segmentation_mask, AREA_THRESHOLD);

    Mat out_of_field_mask;
    mask_region_growing(final_segmentation_mask, out_of_field_mask, {Point(0, 0)});
    bitwise_or(final_segmentation_mask.clone(), out_of_field_mask, final_segmentation_mask);
    imshow("segmentation - balls_localizer::localize", final_segmentation_mask);

    const int HOUGH_MIN_RADIUS = 8;
    const int HOUGH_MAX_RADIUS = 16;
    const float HOUGH_DP = 0.3;
    const int HOUGH_MIN_DISTANCE = 15;
    const int HOUGH_CANNY_PARAM = 100;
    const int HOUGH_MIN_VOTES = 5;
    vector<Vec3f> circles;
    HoughCircles(final_segmentation_mask, circles, HOUGH_GRADIENT, HOUGH_DP, HOUGH_MIN_DISTANCE, HOUGH_CANNY_PARAM, HOUGH_MIN_VOTES, HOUGH_MIN_RADIUS, HOUGH_MAX_RADIUS);

    vector<Mat> hough_circle_masks;
    circles_masks(circles, hough_circle_masks, src.size());
    filter_empty_circles(circles, hough_circle_masks, final_segmentation_mask, 0.60);
    filter_out_of_bound_circles(circles, playing_field.mask, 20);
    filter_near_holes_circles(circles, playing_field.hole_points, 27);
    filter_close_dissimilar_circles(circles, 25, 25, 2);

    Mat display = blurred.clone();
    draw_circles(src, display, circles);

    find_cue_ball(masked, final_segmentation_mask, circles);
    find_black_ball(masked, final_segmentation_mask, circles);
    find_stripe_balls(masked, final_segmentation_mask, circles);

    Vec3f white_ball_circle = localization.cue.circle;
    int white_ball_radius = white_ball_circle[2];
    Point white_ball_center = Point(white_ball_circle[0], white_ball_circle[1]);
    circle(display, white_ball_center, 1, Scalar(0, 100, 100), 1, LINE_AA);
    circle(display, white_ball_center, white_ball_radius, Scalar(255, 255, 255), 2, LINE_AA);

    if (localization.black != NO_LOCALIZATION)
    {
        Vec3f black_ball_circle = localization.black.circle;
        int black_ball_radius = black_ball_circle[2];
        Point black_ball_center = Point(black_ball_circle[0], black_ball_circle[1]);
        circle(display, black_ball_center, 1, Scalar(0, 100, 100), 1, LINE_AA);
        circle(display, black_ball_center, black_ball_radius, Scalar(0, 0, 0), 2, LINE_AA);
    }

    get_bounding_boxes(circles, bounding_boxes);

    imshow("display - end of balls_localizer::localize", display);
}

void balls_localizer::filter_close_dissimilar_circles(vector<Vec3f> &circles, float neighborhood_threshold, float distance_threshold, float radius_threshold)
{
    vector<bool> to_remove(circles.size(), false);

    for (size_t i = 0; i < circles.size(); ++i)
    {
        for (size_t j = 0; j < circles.size(); ++j)
        {
            if (i != j && norm(circles.at(i) - circles.at(j)) < neighborhood_threshold)
            {
                float y1 = circles.at(i)[1];
                float radius_1 = circles.at(i)[2];
                float y2 = circles.at(j)[1];
                float radius_2 = circles.at(j)[2];

                if (y2 > y1 && abs(y2 - y1) < distance_threshold && radius_1 - radius_2 > radius_threshold)
                    to_remove.at(j) = true;
            }
        }
    }

    vector<Vec3f> filtered_circles;
    for (int i = 0; i < circles.size(); i++)
    {
        if (!to_remove[i])
            filtered_circles.push_back(circles.at(i));
    }

    circles = filtered_circles;
}

void balls_localizer::circles_masks(const vector<Vec3f> &circles, vector<Mat> &masks, Size mask_size)
{
    masks.clear();
    for (size_t i = 0; i < circles.size(); i++)
    {
        Mat mask(mask_size, CV_8U);
        mask.setTo(Scalar(0));
        circle(mask, Point(circles[i][0], circles[i][1]), circles[i][2], Scalar(255), FILLED);
        masks.push_back(mask);
    }
}

void balls_localizer::filter_empty_circles(vector<Vec3f> &circles, const vector<Mat> &masks, const Mat &segmentation_mask, float intersection_threshold)
{
    vector<Vec3f> filtered_circles;
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

void balls_localizer::filter_out_of_bound_circles(vector<Vec3f> &circles, const Mat &table_mask, int distance_threshold)
{
    vector<Vec3f> filtered_circles;
    Mat shrinked_table_mask;
    erode(table_mask, shrinked_table_mask, getStructuringElement(MORPH_CROSS, Size(distance_threshold, distance_threshold)));

    for (const Vec3f circle : circles)
    {
        Point center = Point(circle[0], circle[1]);
        if (shrinked_table_mask.at<uchar>(center) == 255)
        {
            filtered_circles.push_back(circle);
        }
    }
    circles = filtered_circles;
}

void balls_localizer::filter_near_holes_circles(vector<Vec3f> &circles, const vector<Point> &holes_points, float distance_threshold)
{
    vector<Vec3f> filtered_circles;
    for (const Vec3f circle : circles)
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

void balls_localizer::extract_seed_points(const Mat &inrange_segmentation_mask, vector<Point> &seed_points)
{
    seed_points.clear();

    for (int y = 0; y < inrange_segmentation_mask.rows; ++y)
    {
        for (int x = 0; x < inrange_segmentation_mask.cols; ++x)
        {
            if (inrange_segmentation_mask.at<uchar>(y, x) > 0)
            {
                seed_points.push_back(Point(x, y));
            }
        }
    }
}

void balls_localizer::fill_small_holes(Mat &binary_mask, double area_threshold)
{
    CV_Assert(binary_mask.type() == CV_8UC1);

    vector<vector<Point>> contours;
    vector<Vec4i> hierarchy;
    findContours(binary_mask, contours, hierarchy, RETR_CCOMP, CHAIN_APPROX_SIMPLE);

    for (int i = 0; i < contours.size(); ++i)
    {
        double area = contourArea(contours[i]);
        if (area < area_threshold)
            drawContours(binary_mask, contours, i, Scalar(255), FILLED, 8, hierarchy, 1); // Fills the hole
    }
}

void balls_localizer::get_bounding_boxes(const vector<Vec3f> &circles, vector<Rect> &bounding_boxes) // TODO move to dedicated lib
{
    for (const Vec3f circle : circles)
    {
        int center_x = static_cast<int>(circle[0]);
        int center_y = static_cast<int>(circle[1]);
        int radius = static_cast<int>(circle[2]);

        // Calculate top-left corner of the bounding box
        int x = center_x - radius;
        int y = center_y - radius;

        // Calculate width and height of the bounding box
        int width = 2 * radius;
        int height = 2 * radius;

        Rect roi(x, y, width, height);
        bounding_boxes.push_back(roi);
    }
}

cv::Rect balls_localizer::get_bounding_box(cv::Vec3f circle) // TODO move to dedicated lib
{
    int center_x = static_cast<int>(circle[0]);
    int center_y = static_cast<int>(circle[1]);
    int radius = static_cast<int>(circle[2]);

    // Calculate top-left corner of the bounding box
    int x = center_x - radius;
    int y = center_y - radius;

    // Calculate width and height of the bounding box
    int width = 2 * radius;
    int height = 2 * radius;

    return Rect(x, y, width, height);
}

float balls_localizer::get_white_ratio_in_circle_cue(const Mat &src, const Mat &segmentation_mask, Vec3f circle)
{
    int x = cvRound(circle[0]);
    int y = cvRound(circle[1]);
    int radius = cvRound(circle[2]);

    Mat mask = Mat::zeros(src.size(), CV_8U);
    cv::circle(mask, Point(x, y), radius, Scalar(255), FILLED);
    Mat balls_segmentation_mask;
    bitwise_not(segmentation_mask, balls_segmentation_mask);
    bitwise_and(mask, balls_segmentation_mask, mask);

    Mat masked_hsv;
    src.copyTo(masked_hsv, mask);

    Mat white_mask;
    const Vec3b WHITE_HSV_LOWERBOUND = Vec3b(20, 0, 180);
    const Vec3b WHITE_HSV_UPPERBOUND = Vec3b(110, 100, 255);
    inRange(masked_hsv, WHITE_HSV_LOWERBOUND, WHITE_HSV_UPPERBOUND, white_mask);

    int white_pixels = countNonZero(white_mask);
    int total_circle_pixels = countNonZero(mask);
    double percentage_white = static_cast<double>(white_pixels) / total_circle_pixels;

    return percentage_white;
}

void balls_localizer::remove_connected_components_by_diameter(Mat &mask, double min_diameter)
{
    CV_Assert(mask.type() == CV_8UC1);

    cv::Mat labels, stats, centroids;
    int nLabels = cv::connectedComponentsWithStats(mask, labels, stats, centroids, 8, CV_32S);

    for (int label = 1; label < nLabels; ++label)
    {
        // Start from 1 to skip the background
        cv::Mat component = (labels == label);

        // Find the minimum enclosing circle
        std::vector<cv::Point> points;
        cv::findNonZero(component, points);
        cv::Point2f center;
        float radius;
        cv::minEnclosingCircle(points, center, radius);

        // Calculate the diameter
        double diameter = 2 * radius;

        // If the diameter is outside the specified range, remove the component
        if (diameter < min_diameter)
            mask.setTo(0, component);
    }
}

float balls_localizer::get_white_ratio_in_circle_stripes(const Mat &src, const Mat &segmentation_mask, Vec3f circle)
{
    int x = cvRound(circle[0]);
    int y = cvRound(circle[1]);
    int radius = cvRound(circle[2]);

    Mat mask = Mat::zeros(src.size(), CV_8U);
    cv::circle(mask, Point(x, y), radius, Scalar(255), FILLED);
    Mat balls_segmentation_mask;
    bitwise_not(segmentation_mask, balls_segmentation_mask);
    bitwise_and(mask, balls_segmentation_mask, mask);

    Mat masked_hsv;
    src.copyTo(masked_hsv, mask);

    Mat white_mask;
    const Vec3b WHITE_HSV_LOWERBOUND = Vec3b(0, 0, 95);
    const Vec3b WHITE_HSV_UPPERBOUND = Vec3b(120, 100, 255);

    inRange(masked_hsv, WHITE_HSV_LOWERBOUND, WHITE_HSV_UPPERBOUND, white_mask);
    remove_connected_components_by_diameter(white_mask, 8);

    int white_pixels = countNonZero(white_mask);
    int total_circle_pixels = countNonZero(mask);
    double percentage_white = static_cast<double>(white_pixels) / total_circle_pixels;

    return percentage_white;
}

float balls_localizer::get_black_ratio_in_circle(const Mat &src, const Mat &segmentation_mask, Vec3f circle)
{
    int x = cvRound(circle[0]);
    int y = cvRound(circle[1]);
    int radius = cvRound(circle[2]);

    Mat mask = Mat::zeros(src.size(), CV_8U);
    cv::circle(mask, Point(x, y), radius, Scalar(255), FILLED);
    Mat balls_segmentation_mask;
    bitwise_not(segmentation_mask, balls_segmentation_mask);
    bitwise_and(mask, balls_segmentation_mask, mask);

    Mat masked_hsv;
    src.copyTo(masked_hsv, mask);

    Mat black_mask;
    const Vec3b BLACK_HSV_LOWERBOUND = Vec3b(35, 1, 0);
    const Vec3b BLACK_HSV_UPPERBOUND = Vec3b(140, 255, 90);
    inRange(masked_hsv, BLACK_HSV_LOWERBOUND, BLACK_HSV_UPPERBOUND, black_mask);

    int white_pixels = countNonZero(black_mask);
    int total_circle_pixels = countNonZero(mask);
    double percentage_black = static_cast<double>(white_pixels) / total_circle_pixels;

    return percentage_black;
}

float balls_localizer::mean_squared_bgr_intra_pixel_difference(const Mat &src, const Mat &segmentation_mask, Vec3f circle)
{
    Mat mask = Mat::zeros(src.size(), CV_8U);
    cv::circle(mask, Point(cvRound(circle[0]), cvRound(circle[1])), cvRound(circle[2]), Scalar(255), FILLED);
    Mat balls_segmentation_mask;
    bitwise_not(segmentation_mask, balls_segmentation_mask);
    bitwise_and(mask, balls_segmentation_mask, mask);

    double count = 0;
    for (int y = 0; y < src.rows; y++)
    {
        for (int x = 0; x < src.cols; x++)
        {
            if (mask.at<uchar>(y, x) == 255)
            {
                double b = static_cast<double>(src.at<Vec3b>(y, x)[0]);
                double g = static_cast<double>(src.at<Vec3b>(y, x)[1]);
                double r = static_cast<double>(src.at<Vec3b>(y, x)[2]);
                count += pow(255 - b, 2) + pow(255 - g, 2) + pow(255 - r, 2);
            }
        }
    }
    return count / countNonZero(mask);
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

void balls_localizer::draw_circles(const cv::Mat &src, cv::Mat &dst, std::vector<cv::Vec3f> &circles)
{
    dst = src.clone();
    for (const Vec3f circle : circles)
    {
        Point center = Point(circle[0], circle[1]);
        cv::circle(dst, center, 1, Scalar(0, 100, 100), 1, LINE_AA);
        int radius = circle[2];
        cv::circle(dst, center, radius, Scalar(255, 0, 255), 1, LINE_AA);
    }
}

void balls_localizer::find_cue_ball(const Mat &src, const Mat &segmentation_mask, const vector<Vec3f> &circles)
{
    Mat src_hsv;
    cvtColor(src, src_hsv, COLOR_BGR2HSV);

    // We compute, for each circle, the percentage of "white" pixels. The one with highest percentage is picked as white ball.
    vector<pair<Vec3f, float>> circles_white_percentages;
    for (Vec3f circle : circles)
    {
        circles_white_percentages.push_back({circle, get_white_ratio_in_circle_cue(src_hsv, segmentation_mask, circle)});
    }

    //  Sort by descending order of percentage
    std::sort(circles_white_percentages.begin(), circles_white_percentages.end(), [](const pair<Vec3f, float> &a, const pair<Vec3f, float> &b)
              { return a.second > b.second; });

    Vec3f white_ball_circle;
    if (circles_white_percentages.at(0).second - circles_white_percentages.at(0).second > 0.1)
        white_ball_circle = circles_white_percentages.at(0).first;
    else
    {
        // Tie break
        double difference_0 = mean_squared_bgr_intra_pixel_difference(src, segmentation_mask, circles_white_percentages.at(0).first);
        double difference_1 = mean_squared_bgr_intra_pixel_difference(src, segmentation_mask, circles_white_percentages.at(1).first);
        white_ball_circle = difference_0 < difference_1 ? circles_white_percentages.at(0).first : circles_white_percentages.at(1).first;
    }

    localization.cue.circle = white_ball_circle;
    localization.cue.bounding_box = get_bounding_box(white_ball_circle);
}

void balls_localizer::find_black_ball(const Mat &src, const Mat &segmentation_mask, const vector<Vec3f> &circles)
{
    Mat src_hsv;
    cvtColor(src, src_hsv, COLOR_BGR2HSV);

    // We compute, for each circle, the percentage of "white" pixels. The one with highest percentage is picked as white ball.
    vector<pair<Vec3f, float>> circles_black_ratios;
    for (Vec3f circle : circles)
    {
        circles_black_ratios.push_back({circle, get_black_ratio_in_circle(src_hsv, segmentation_mask, circle)});
    }

    // Sort by descending order of percentage
    std::sort(circles_black_ratios.begin(), circles_black_ratios.end(), [](const pair<Vec3f, float> &a, const pair<Vec3f, float> &b)
              { return a.second > b.second; });

    const float RATIO_THRESHOLD = 0.5;
    if (circles_black_ratios.at(0).second > RATIO_THRESHOLD)
    {
        localization.black.circle = circles_black_ratios.at(0).first;
        localization.black.bounding_box = get_bounding_box(circles_black_ratios.at(0).first);
    }
    else
    {
        // No black circle detected
        localization.black = NO_LOCALIZATION;
    }
}

void balls_localizer::find_stripe_balls(const Mat &src, const Mat &segmentation_mask, const vector<Vec3f> &circles)
{
    Mat src_hsv;
    cvtColor(src, src_hsv, COLOR_BGR2HSV);

    // We compute, for each circle, the percentage of "white" pixels.
    vector<pair<Vec3f, float>> circles_white_ratios;
    for (Vec3f circle : circles)
    {
        circles_white_ratios.push_back({circle, get_white_ratio_in_circle_stripes(src_hsv, segmentation_mask, circle)});
    }

    //  Sort by descending order of percentage
    std::sort(circles_white_ratios.begin(), circles_white_ratios.end(), [](const pair<Vec3f, float> &a, const pair<Vec3f, float> &b)
              { return a.second > b.second; });

    vector<pair<Vec3f, float>> circles_white_ratios_filtered;
    std::copy_if(circles_white_ratios.begin(), circles_white_ratios.end(), std::back_inserter(circles_white_ratios_filtered), [](pair<Vec3f, float> p)
                 { return p.second >= 0.17 && p.second <= 0.81; });

    vector<pair<Vec3f, float>> circles_stripes;
    std::copy_if(circles_white_ratios_filtered.begin(), circles_white_ratios_filtered.end(), std::back_inserter(circles_stripes), [this](pair<Vec3f, float> p)
                 { return p.first != this->localization.cue.circle && p.first != this->localization.black.circle; });

    for (auto p : circles_white_ratios)
    {
        cout << p.first << p.second << endl;
    }
    cout << "------filtered-----" << endl;

    for (auto p : circles_stripes)
    {
        cout << p.first << p.second << endl;
    }
    cout << "-----------" << endl;

    vector<Vec3f> temp_circles;
    for (auto t : circles_stripes)
    {
        temp_circles.push_back(t.first);
    }

    Mat temp = src.clone();
    draw_circles(src, temp, temp_circles);
    imshow("temp", temp);
}

void balls_localizer::find_solid_balls(const Mat &src, const Mat &segmentation_mask, const vector<Vec3f> &circles)
{
}