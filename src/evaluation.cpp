#include "performance_measurement.h"
#include "frame_segmentation.h"

#include <iostream>

using namespace std;
using namespace cv;

int main()
{
    vector<String> filenames;
    vector<Mat> found_table_masks;
    vector<Mat> ground_truth_table_masks;
    vector<String> frames_filenames;
    vector<balls_localization> predicted_balls_localizations;
    vector<balls_localization> ground_truth_balls_localizations;

    glob("*.png", filenames, true);
    for (auto filename : filenames)
    {
        if (filename.find("masks") == String::npos && (filename.find("first") != String::npos || filename.find("last") != String::npos))
        {
            Mat img = imread(filename);
            Mat segm;
            get_colored_frame_segmentation(img, segm, false);
            imshow("yo", segm);
            waitKey();
        }
    }
    return 0;

    glob("*.png", filenames, true);
    for (auto filename : filenames)
    {
        if (filename.find("masks") == String::npos && (filename.find("first") != String::npos || filename.find("last") != String::npos))
        {
            Mat img = imread(filename);
            Mat img_segmentation;
            balls_localization localization;

            get_frame_segmentation(img, img_segmentation);
            get_balls_localization(img, localization);

            found_table_masks.push_back(img_segmentation);
            frames_filenames.push_back(filename);
            predicted_balls_localizations.push_back(localization);
        }
    }

    // performance computation
    for (auto filename : filenames)
    {
        if (filename.find("masks") != String::npos && (filename.find("first") != String::npos || filename.find("last") != String::npos))
        {
            Mat img = imread(filename, CV_8UC1);
            ground_truth_table_masks.push_back(img);
        }
    }

    // Bounding box load
    vector<String> txt_filenames;
    glob("*.txt", txt_filenames, true);
    for (auto txt_filename : txt_filenames)
    {
        if (txt_filename.find("bounding_boxes") != String::npos && (txt_filename.find("first") != String::npos || txt_filename.find("last") != String::npos))
        {
            balls_localization ground_truth;
            load_ground_truth_localization(txt_filename, ground_truth);
            ground_truth_balls_localizations.push_back(ground_truth);
        }
    }

    for (int i = 0; i < found_table_masks.size(); i++)
    {
        cout << frames_filenames.at(i) << endl;
        evaluate_balls_and_playing_field_segmentation(found_table_masks[i], ground_truth_table_masks[i]);
        evaluate_balls_localization(predicted_balls_localizations[i], ground_truth_balls_localizations[i]);
        cout << endl << endl;
    }

    return 0;
}