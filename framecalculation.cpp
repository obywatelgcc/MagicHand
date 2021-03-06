#include "framecalculation.h"
#include <algorithm>
#include <cmath>

#define M_PI		3.14159265358979323846
#define M_PI_2		1.57079632679489661923

FrameCalculation::FrameCalculation(string window_name, bool video_from_file, float video_speed)
{
    this->window_name = window_name;
    frame_counter_mode = video_from_file;
    if(video_speed < 0.01) video_speed = 20;
    frame_duration = 1000.0 / video_speed;

    average_color = Vec3i(60, 100, 100);
    lower_color = Vec3i(40, 50, 50);
    upper_color = Vec3i(80, 150, 150);
    rgb_color = Vec3i(0, 255, 0);

    identified = false;
    calibration_windows_size = 50;
    calibration_time_counter = 0;
    max_calibration_time_counter = 5000;

    state = AS_CALIBRATION;
    state_time_counter = 0;
    max_state_time_counter = 2000;

    min_contour_size = 20;

    morphology_element = Mat(3, 3, CV_8UC1, Scalar(0,0,0));
    circle( morphology_element, Point( morphology_element.cols/2, morphology_element.rows/2), morphology_element.rows/2, 255, -1);

    last_time = current_time = std::chrono::high_resolution_clock::now();
}

FrameCalculation::~FrameCalculation()
{
    for(auto s : shapes) delete s;
}

void FrameCalculation::calculate(Mat &frame)
{
    int dt;
    if(frame_counter_mode)
    {
        dt = frame_duration;
        if(state != AS_CALIBRATION) dt *= 3;
    }
    else
    {
        current_time = std::chrono::high_resolution_clock::now();
        dt = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_time).count();
        last_time = current_time;
    }
//    cout << (state != AS_CALIBRATION ) << " " << dt << endl;

    if(state != AS_CALIBRATION)
    {
        frame.copyTo(output);
        selecPointers(frame); //szukanie konturów wskaźników
        alpha_buffor = Mat::zeros(frame.rows, frame.cols, frame.type());

        //usunięcie zbędnych obiektów z kontenera
        shapes.erase(std::remove_if(shapes.begin(), shapes.end(), [](Shape* s){ if(!s->isValid()) {delete s; return true;} return false; }), shapes.end());

        for(auto s : shapes) s->drawShape(alpha_buffor, dt);

        if(pointers_contours.size() == 1)//rysowanie figur
        {
            if(state != AS_DRAWING && state_time_counter > 0) state_time_counter -= dt; //czekamy przez chwilę zanim możemy zmienić stan
            if(state_time_counter <= 0 && state != AS_DRAWING) //zezwalamy na rozpoczęcie rysowania
            {
                state = AS_DRAWING;
                draw_points.clear();
            }
            if(state == AS_DRAWING) //dodajemy kolejne punkty
            {
                state_time_counter += dt;
                if(state_time_counter > max_state_time_counter) state_time_counter = max_state_time_counter;

                draw_points.push_back(Contour::contourCenter(pointers_contours.at(0)));
                identified = false;
                if(draw_points.size() > min_contour_size)
                {
                    double x, y, d;
                    for(int i = 0; i < min_contour_size / 2; i++) //sprawdzamy czy zamkneliśmy rysowaną figurę
                    {
                        x = draw_points[draw_points.size() - 1].x - draw_points[i].x;
                        y = draw_points[draw_points.size() - 1].y - draw_points[i].y;
                        d = sqrt(x*x + y*y);
                        //jeśli koniec zetkną się z początkiem o ile nie staliśmy w jednym miejscu to koniec rysowanie figury
                        if(d < (min_contour_size / 2 - 1) && (contourArea(draw_points) > M_PI * min_contour_size * min_contour_size / 4))
                        {
                            state = AS_SHOWING;
                            state_time_counter = max_state_time_counter;
                        }
                    }
                }
            }
        }
        else if(pointers_contours.size() == 2) //usuwanie figur
        {
            if(state != AS_REMOVING && state_time_counter > 0) state_time_counter -= dt; //czekamy przez chwilę zanim możemy zmienić stan
            if(state_time_counter <= 0 && state != AS_REMOVING) //zezwalamy na rozpoczęcie usuwania
            {
                state = AS_REMOVING;
                draw_points.clear();
            }
            if(state == AS_REMOVING)
            {
                state_time_counter++;
                if(state_time_counter > max_state_time_counter / 2) state_time_counter = max_state_time_counter / 2;

                vector<Point> pointers_centers;
                for(auto c : pointers_contours)
                    pointers_centers.push_back(Contour::contourCenter(c));

                for(auto s : shapes)
                    if(s->removeShape(pointers_centers, dt)) break;
            }
        }
        else if(pointers_contours.size() > 2) //przenoszenie figur
        {
            if(state != AS_MOVING && state_time_counter > 0) state_time_counter -= dt; //czekamy przez chwilę zanim możemy zmienić stan
            if(state_time_counter <= 0 && state != AS_MOVING) //zezwalamy na rozpoczęcie przenoszenia
            {
                state = AS_MOVING;
                draw_points.clear();
            }
            if(state == AS_MOVING)
            {
                state_time_counter += dt;
                if(state_time_counter > max_state_time_counter) state_time_counter = max_state_time_counter;

                vector<Point> pointers_centers;
                for(auto c : pointers_contours)
                    pointers_centers.push_back(Contour::contourCenter(c));

                Contour::sortPoints(pointers_centers);
                Point pc = Contour::contourCenter(pointers_centers);
                int x, y;
                double d;
                for(auto s : shapes)
                {
                    x = s->center.x - pc.x;
                    y = s->center.y - pc.y;
                    d = sqrt(x*x + y*y);
                    if(d < 40)
                    {
                        s->moveShapeTo(pc);
                        break;
                    }
                }

                Contour::drawPoly(alpha_buffor, pointers_centers, pc, Scalar(rgb_color[0], rgb_color[1], rgb_color[2]));
            }
        }
        else //nie ma wskaźnika na ekranie
        {
            if(state != AS_SHOWING) state_time_counter -= dt;
            if(state_time_counter <= 0 && state != AS_SHOWING) state = AS_SHOWING;
            if(state == AS_SHOWING)
            {
                state_time_counter += dt;
                if(state_time_counter > max_state_time_counter / 2) state_time_counter = max_state_time_counter / 2;
                draw_points.clear();
            }
        }

        //narysowanie wskaźników
        for(int i = 0; i < pointers_contours.size(); i++)
            drawContours(output, pointers_contours, i, Scalar(rgb_color[0], rgb_color[1], rgb_color[2]), -1);


        if(state != AS_DRAWING) //jeśli rysownaie się skończyło to rozpoznaj kształt
        {
            if(!identified)addShape(); //jeśli nie rozpoznano jeszcze narysowanego kształtu
        }
        else //podczas rysowania
        {
            polylines(output, vector<vector<Point> >(1,draw_points), false, Scalar(rgb_color[0], 255, rgb_color[2]), 3);
        }

        output = Contour::alphaBlend(output, alpha_buffor, 0.8);
    }
    else
    {
        calibration(frame, dt);
    }
    imshow(window_name, output);
}

void FrameCalculation::calibration(Mat &frame, int dt)
{
    frame.copyTo(output);
    contours.clear();

    Rect cr = Rect((frame.cols-calibration_windows_size)/2, (frame.rows-calibration_windows_size)/2,
                   calibration_windows_size, calibration_windows_size);
    Mat cm = Mat(frame,cr);
    cvtColor(cm, hsv, CV_RGB2HSV);
    Scalar tmp_sum = sum(hsv);

    average_color = Vec3i(tmp_sum[0]/hsv.total(), tmp_sum[1]/hsv.total(), tmp_sum[2]/hsv.total()); //średni kolor hsv
    lower_color = Vec3i((average_color[0] - 20 + lower_color[0]) / 2, (average_color[1] - 100 + lower_color[1])/2, (average_color[2] - 100 + lower_color[2])/2);
    upper_color = Vec3i((average_color[0] + 20 + upper_color[0]) / 2, 240, 240);

    if(lower_color[0] < 0) lower_color[0] = 0;
    if(lower_color[1] < 0) lower_color[1] = 0;
    if(lower_color[2] < 0) lower_color[2] = 0;

    if(upper_color[0] > 180) upper_color[0] = 180;
    if(upper_color[1] > 255) upper_color[1] = 255;
    if(upper_color[2] > 255) upper_color[2] = 255;

    rgb_color = HSVToRGBColor(average_color);

    inRange(hsv, lower_color, upper_color, binary);

    morphologyEx(binary, binary, MORPH_CLOSE, morphology_element, Point(-1, -1), 1);
    double area_sum = countMaskPixels(binary);
    findContours(binary, contours, CV_RETR_LIST , CV_CHAIN_APPROX_NONE);

    for(int i = 0; i < contours.size(); i++)
    {
        drawContours(output, contours, i, Scalar(rgb_color[0], rgb_color[1], rgb_color[2]), -1, 8, noArray(), INT_MAX,
                Point((frame.cols-calibration_windows_size)/2, (frame.rows-calibration_windows_size)/2));
    }
    if(area_sum / cm.total() > 0.95)
    {
        calibration_time_counter += dt;
        if(calibration_time_counter > max_calibration_time_counter) state = AS_SHOWING;
    }
    else if(calibration_time_counter > 0) calibration_time_counter -= dt;

    rectangle(output, cr, Scalar(255, 0, 0), 2);
    string text = "Przedzial koloru: (" + intToString(lower_color[0]) + "; " + intToString(lower_color[1]) + "; " + intToString(lower_color[2]) + ") - "
            +  "(" + intToString(upper_color[0]) + "; " + intToString(upper_color[1]) + "; " + intToString(upper_color[2]) + ")";
    putText(output, text, Point(10, 20), FONT_HERSHEY_SIMPLEX, 0.7, Scalar(255, 0, 0), 2);
    text = "Wypelnienie: " + intToString(area_sum / cm.total() * 100) + "%";
    putText(output, text, Point(10, 40), FONT_HERSHEY_SIMPLEX, 0.7, Scalar(255, 0, 0), 2);
    text = "Kalibracja: " + intToString(calibration_time_counter * 100 / max_calibration_time_counter) + "%";
    putText(output, text, Point(10, 60), FONT_HERSHEY_SIMPLEX, 0.7, Scalar(255, 0, 0), 2);
}

void FrameCalculation::selecPointers(Mat &frame)
{
    pointers_contours.clear();
    contours.clear();
    cvtColor(frame, hsv, CV_RGB2HSV);
    inRange(hsv, lower_color, upper_color, binary);
    morphologyEx(binary, binary, MORPH_CLOSE, morphology_element, Point(-1, -1), 1);

    findContours(binary, contours, CV_RETR_LIST , CV_CHAIN_APPROX_NONE);

    double area = 0;
    double mc = 0;
    for(int i = 0; i < contours.size(); i++)
    {
        area = contourArea(contours[i]);
        mc = Contour::malinowskaCoefficient(contours[i]);
        if( mc < 0.35 && area > 530)
        {
            pointers_contours.push_back(contours[i]);
        }
    }
}

void FrameCalculation::addShape()
{
    identified = true;
    if(draw_points.size() < min_contour_size) return;
    vector<Point> draw_contour;
    approxPolyDP(draw_points, draw_contour, 2 , true);

    Shape* shape  = new Shape(draw_contour);
    if(shape->isValid())
    {
        shapes.push_back(shape);
    }
}

string FrameCalculation::intToString(int num)
{
    if(num == 0) return "0";

    std::string buf;
    bool negative = false;
    if(num < 0)
    {
        negative = true;
        num = -num;
    }
    for(; num; num /= 10) buf = "0123456789abcdef"[num % 10] + buf;
    if(negative) buf = "-" + buf;
    return buf;
}

Vec3i FrameCalculation::HSVToRGBColor(Vec3i& hsv)
{
    Mat RGB;
    Mat HSV(1, 1, CV_8UC3, Scalar(hsv[0], hsv[1], hsv[2]));
    cvtColor(HSV, RGB,CV_HSV2RGB);
    return RGB.at<Vec3b>(0,0);
}

int FrameCalculation::countMaskPixels(Mat &mask)
{
    int sum = 0;
    for(int i = 0; i < mask.cols; i++)
        for(int j = 0; j < mask.rows; j++)
            if(mask.at<unsigned char>(i, j) > 0) sum++;
    return sum;
}
