#include <iostream>
#include <cmath>
#include <map>
#include "playing_field_localizer.h"

using namespace cv;
using namespace std;

void playing_field_localizer::segmentation(const Mat src, Mat &dst)
{
    Mat src_hsv;
    cvtColor(src, src_hsv, COLOR_BGR2HSV);

    dst = src_hsv;

    Mat data;
    dst.convertTo(data, CV_32F);
    data = data.reshape(1, data.total());

    // do kmeans
    Mat labels, centers;
    kmeans(data, 5, labels, TermCriteria(TermCriteria::MAX_ITER, 10, 1.0), 3,
           KMEANS_PP_CENTERS, centers);

    // reshape both to a single row of Vec3f pixels:
    centers = centers.reshape(3, centers.rows);
    data = data.reshape(3, data.rows);

    // replace pixel values with their center value:
    Vec3f *p = data.ptr<Vec3f>();
    for (size_t i = 0; i < data.rows; i++)
    {
        int center_id = labels.at<int>(i);

        if (center_id == 0)
        {
            p[i] = centers.at<Vec3f>(center_id);
        }
        else
        {
            p[i] = Vec3f(0, 0, 0);
        }
        p[i] = centers.at<Vec3f>(center_id);
    }

    // back to 2d, and uchar:
    dst = data.reshape(3, dst.rows);
    dst.convertTo(dst, CV_8U);

    // -------
}

void playing_field_localizer::localize(const Mat src, Mat &dst)
{
    Mat segmented, labels;
    segmentation(src, segmented);

    imshow("", segmented);
    waitKey(0);
}