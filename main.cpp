#include <cstdio>
#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <fstream>

using namespace std;

const string EXP_DIR = "runs/detect/exp/"; // YOLOv7 출력 저장위치
const int HEADER_SIZE = 54; // 헤더 크기

vector<tuple<double, double, double, double>> xywh_arr; // 마스크를 쓰지 않은 사람들 얼굴의 x, y 좌표와 너비 w, 높이 h
int strength; // 모자이크 효과 세기(강도)

// 문자열이 정수인지 체크하는 함수
inline bool is_number(const string& s) {
    return !s.empty() && find_if(s.begin(), s.end(), [](unsigned char c) { return !isdigit(c); }) == s.end();
}

// 모자이크 효과 처리를 위한 함수
inline int coord_to_mosaic_coord(const int& x) {
    return (x / strength) * strength;
}

// 헤더 파일에서 필요한 정보 추출
tuple<int, int> read_header(ifstream& in, ofstream& out) {
    int width = 0, height = 0;
    
    // 헤더를 복사하면서 정보를 읽음
    for(int i = 0; i < HEADER_SIZE; i++) {
        char c;
        in.get(c);
        
        // width, height을 리틀 엔디안으로 받아 변환
        if(i >= 18 && i <= 21) width += (unsigned char) c * (1 << (8 * (i - 18)));
        if(i >= 22 && i <= 25) height += (unsigned char) c * (1 << (8 * (i - 22)));
        
        out << c;
    }
    
    printf("    found image of width: %d height: %d\n", width, height);
    
    // 모자이크 처리 세기가 0으로 입력된 경우(기본값으로 설정)
    if(strength == 0) {
        strength = ((width / 30) + (height / 30)) / 2;
        cout << "   Applying default strength: " << strength << endl;
    }
    return make_tuple(width, height);
}

// YOLOv7의 출력을 읽어 마스크를 쓰지 않은 사람들의 x, y, w, h 를 읽어냄
void getxywh(const string& filename) {
    string dir = EXP_DIR + "labels/" + filename + ".txt";
    ifstream in(dir, ios::in);
    
    // YOLOv7의 출력은 "[class] [x] [y] [w] [h]" 로 되어 있기에 마스크를 안 썼을 때의 class 값인 '2' 만 읽는 코드
    string line;
    while(getline(in, line)) {
        if(line[0] == '0') continue; // 마스크 쓴 경우 continue
        else { // line[0] ([class]를 나타냄) == '2'
            stringstream ss(line);
            string tok;
            
            double xywh[4];
            int index = 0;
            getline(ss, tok, ' ');
            while(getline(ss, tok, ' ')) {
                xywh[index] = stod(tok);
                index++;
            }
            
            xywh_arr.push_back({xywh[0], xywh[1], xywh[2], xywh[3]});
        }
    }
    
    in.close();
}

// getxywh로 읽은 x, y, w, h를 바탕으로 bmp 파일을 편집하여 마스크를 안 쓴 사람의 얼굴을 모자이크 처리 함
void drawxywh(const string& filename, const tuple<double, double, double, double>& xywh) {
    string dir = filename + ".bmp" , dest_dir = "copy_" + filename + ".bmp";
    
    ifstream in(dir, ios::in | ios::binary);
    ofstream out(dest_dir, ios::out | ios::binary);
    
    // 헤더 정보 읽기
    int width, height;
    cout << "   reading header..." << endl;
    tie(width, height) = read_header(in, out);
    
    // YOLOv7은 이미지의 가로 세로에 대해 상대적인 값을 출력하므로 가로 세로를 곱하여 절대적인 픽셀 좌표로 변환
    double box_x_d, box_y_d, box_w_d, box_h_d; // double(상대적인 값)
    tie(box_x_d, box_y_d, box_w_d, box_h_d) = xywh;
    
    int box_x = (int) (box_x_d * width), box_w = (int) (box_w_d * width);
    int box_y = (int) (box_y_d * height), box_h = (int) (box_h_d * height);
    
    int min_x = box_x - box_w / 2, max_x = box_x + box_w / 2;
    int min_y = height - (box_y + box_h / 2), max_y = height - (box_y - box_h / 2);
    
    printf("    found person with no mask(x: %d, y: %d, w: %d, h: %d)\n", box_x, box_y, box_w, box_h);
    
    // R, G, B 값 읽기
    unsigned char R[width][height], G[width][height], B[width][height];
    
    // 원본 파일 읽기
    for(int h = 0; h < height; h++) {
        for(int w = 0; w < width; w++) {
            char c;
            in.get(c);
            B[w][h] = (unsigned char) c;
            in.get(c);
            G[w][h] = (unsigned char) c;
            in.get(c);
            R[w][h] = (unsigned char) c;
        }
    }
    
    // 복사본에 그려넣기
    for(int h = 0; h < height; h++) {
        for(int w = 0; w < width; w++) {
            if(w >= min_x && w <= max_x && h >= min_y && h <= max_y) {
                // 모자이크 처리
                out << B[coord_to_mosaic_coord(w)][coord_to_mosaic_coord(h)];
                out << G[coord_to_mosaic_coord(w)][coord_to_mosaic_coord(h)];
                out << R[coord_to_mosaic_coord(w)][coord_to_mosaic_coord(h)];
            } else {
                // 일반적인 픽셀 복사]
                out << B[w][h];
                out << G[w][h];
                out << R[w][h];
            }
        }
    }
    
    in.close();
    out.close();
}

// drawxywh 함수는 출력된 이미지를 copy_[원본 파일명].bmp 에 저장하므로 다시 복사본을 원본 파일에 덮어씌우는 함수
void copy_to_original(const string& filename) {
    string dir = "copy_" + filename + ".bmp" , dest_dir = filename + ".bmp";
    
    ifstream in(dir, ios::in | ios::binary);
    ofstream out(dest_dir, ios::out | ios::binary);
    
    int width, height;
    cout << "   reading header..." << endl;
    tie(width, height) = read_header(in, out);
    
    char R[width][height], G[width][height], B[width][height];
    
    // 원본 파일 읽기
    for(int h = 0; h < height; h++) {
        for(int w = 0; w < width; w++) {
            char c;
            in.get(c);
            B[w][h] = (unsigned char) c;
            in.get(c);
            G[w][h] = (unsigned char) c;
            in.get(c);
            R[w][h] = (unsigned char) c;
        }
    }
    
    // 복사본에 그려넣기
    for(int h = 0; h < height; h++) {
        for(int w = 0; w < width; w++) {
            out << B[w][h];
            out << G[w][h];
            out << R[w][h];
        }
    }
    
    in.close();
    out.close();
}

int main(int argc, const char * argv[]) {
    cout << "------------ running bmp manipulator: desgined by 22081 이현준 ------------";
    
    getxywh(argv[1]);
    // 마스크를 안 쓴 사람이 없는 경우
    if(xywh_arr.empty()) {
        cout << "No incorrect masks detected." << endl;
        return 0;
    }

    // 모자이크 효과 세기 정도를 입력 받는 코드
    while(true) {
        cout << endl << "Type in the strength of the mosaic effect(0 for default settings): ";
        string strnth = "";
        cin >> strnth;
        
        if(is_number(strnth)) {
            strength = stoi(strnth);
            if(strength >= 0) break;
        }
        
        cout << "Only positive integer or 0 is allowed.";
    }

    for(tuple<double, double, double, double> xywh : xywh_arr) {
        cout << "applying effect..." << endl;
        drawxywh(argv[1], xywh);
        cout << "overwriting original file..." << endl;
        copy_to_original(argv[1]);
    }
    
    cout << "succesfully detected mask!" << endl;
    
    return 0;
}
