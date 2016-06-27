

//number of hits
int hit_counter;

//number of points we've seen; capped at num_points_limit
int num_points;
const int num_points_limit = 10000;

//number of points to use for the mean; the mean is subtracted for
// all points after the first 100
const int num_points_mean = 100;

void setup() {
    Serial.begin(115200);
    hit_counter = 0;
    num_points = 0;
}
int data_point[2];  // stores time and z (I don't use the x and y values)
void loop() {

    //get data and puts it in data_point[]; must modify
    getAccelData();

    //processes each point, and if it is a hit, calls updateCounter()
    process(data_point[0], data_point[1]);
 
}

/* must modify to use !! */
void getAccelData() {

    data_point[0] = 0; //time
    data_point[1] = 0; //z accel
}

/* increments the hit counter and prints it out */
void updateCounter(int t, float z) {
    hit_counter += 1;
    Serial.print("Num_hits: ");
    Serial.print(hit_counter);
    Serial.print(t);
    Serial.print(" ");
    Serial.print(z);
    Serial.print("\n"); 
}


/* We will use a Butterworth filter of order 9, normalized cutoff frequency 4.0/nyf
 where nyf is the Nyquist frequency (250)
 since our samples aren't exactly 500hz, not exactly accurate

 constants for the filter, which we can also get by
 b,a = butter(9,4/250.0) if we were in Python 
 I think they do need to be doubles; floats don't have enough precision
 and the filter is a bit unstable*/
const int order = 9;
double a[] = {-8.7105351748815316, 33.726049364325746, -76.183439550219617, 110.64383340913214, -107.14182626366458, 69.176004487367535, -28.715848459821281, 6.9543946168387691, -0.74863242907538308};
double b[] = {3.46829595e-15,3.12146636e-14,1.24858654e-13,
    2.91336860e-13,4.37005290e-13,4.37005290e-13,
    2.91336860e-13,1.24858654e-13,3.12146636e-14,
    3.46829595e-15};

// window of values for use with the filter
double raw_v[order + 1] = {0};
double filtered_v[order] = {0};

//indices
int raw_i = 0;
int filtered_i = 0;

float get_filtered(float v) {
    raw_v[raw_i] = (double)v;
    double filtered_val = 0;
    for (int j = 0; j < order; j++){
        filtered_val -= a[j] * filtered_v[(filtered_i  - 1- j + order) % order];
        filtered_val += b[j] * raw_v[(raw_i-1 - j + order + 1)%(order+1)];
    } 
    filtered_val += b[order] * raw_v[raw_i];
    filtered_v[filtered_i] = filtered_val;
    raw_i = (raw_i + 1 ) % (order + 1);
    filtered_i = (filtered_i + 1 ) % (order);
    return((float)(filtered_val));
}


// defines window size for sliding window for calculating slope
const int window_size = 44;
const int half_ws = window_size / 2;
//window size for averaging to get current minimum value
const int ws_ave = 10;

//weights for smoothing out values; this is a gaussian with 
//standard deviation 44/3
float kl[] = {0.34148732,  0.3765062 ,  0.4131909 ,  0.45134687,  0.4907397 ,
        0.53109599,  0.57210523,  0.61342276,  0.65467373,  0.69545818,
        0.73535693,  0.77393846,  0.8107664 ,  0.84540754,  0.87744028,
        0.906463  ,  0.93210249,  0.95402186,  0.9719279 ,  0.98557764,
        0.9947838 ,  0.99941907,  0.99941907,  0.9947838 ,  0.98557764,
        0.9719279 ,  0.95402186,  0.93210249,  0.906463  ,  0.87744028,
        0.84540754,  0.8107664 ,  0.77393846,  0.73535693,  0.69545818,
        0.65467373,  0.61342276,  0.57210523,  0.53109599,  0.4907397 ,
        0.45134687,  0.4131909 ,  0.3765062 ,  0.34148732};


int cur_i = 0;
int ws_index = 0;
int hws_index = 0;

/* get the slope at a particular data point by a subtracting a weighted
 average of the first half of a window of points from a weighted average
 of the second half of a window of points */
float get_slope(float* vals){
    float slope = 0;
    for (int i = 0; i < half_ws; i ++){
        slope += kl[(window_size - 1 - i + window_size)%window_size] * vals[(ws_index-1-i + window_size) % window_size]; //second half
        slope -= kl[i] * vals[(ws_index+i)% window_size]; // first half
    }
    return(slope);
}



// sliding window arrays to hold values
float vals[window_size] = {0};
int tvals[window_size] = {0};
float slopes[window_size] = {0};
bool maxima[half_ws] = {false};

const float default_min = 10000;
float cur_min = default_min;

/* struct for the return value
 max_val is the maximum we saw (note that because we have a window size
 that is greater than 1, this means when we determine we have seen a
 maximum, it has already passed)
 cur_min_copy is the most recent minimum
 max_t is the time at maximum 
 is_max tells us if we just saw a maximum */
struct max_data {
    float max_val;
    float cur_min_copy;
    int max_t;
    bool is_max;
};

struct max_data get_maxima_ret_vals;

/* determines if we just saw a maximum, and if we did,
 the values for struct max_data */
void get_maxima(int cur_t) {
    bool is_max = false;
    float max_val = -default_min;
    int max_t = 0;
    float cur_min_copy = 0;
    if (num_points > num_points_mean + window_size + 1){
        float cm = 0;
        for (int i = 0; i < ws_ave; i++){
            cm += vals[(ws_index + 1 + i)%window_size]/((float)ws_ave);
        }
        if (cm < cur_min){
            cur_min = cm;
        }
        int first_count = 0;
        int second_count = 0;

        int k1, k2;

        for (int i = 0; i < half_ws; i++){
            k1 = (ws_index + i)%window_size;
            k2 = (ws_index - 1 -i + window_size)%window_size;
            if (slopes[k1] > 0.25){
                first_count += 1;
            }
            if (slopes[k2] < -0.25){
                second_count += 1;
            }
        }

        bool any_m;
        if ((first_count > half_ws -3) && (second_count > half_ws -3)){
            //make sure not to double count maxima
            any_m = false;
            for (int i = 0; i < half_ws; i++){
                if (maxima[i]){
                    any_m = true;
                }
            }
            if (!any_m) {
                is_max = true;
                max_val = vals[(ws_index + 1)%window_size];
                max_t = tvals[(ws_index + 1)%window_size];
                cur_min_copy = cur_min;
                cur_min = default_min;
            }
        }
        maxima[hws_index] = is_max;
    } 

    
    get_maxima_ret_vals.is_max = is_max;
    get_maxima_ret_vals.max_val = max_val;
    get_maxima_ret_vals.cur_min_copy = cur_min_copy;
    get_maxima_ret_vals.max_t = max_t;

}

/* constants  and arrays for thresholding for determining 
 if they were hits */
const float default_threshold = 24;
const float h_threshold = 35;
const int num_hits_to_keep = 200;
int threshold = default_threshold;
float last_hit = default_threshold;
int last_hit_time = 0;
int hit_times[num_hits_to_keep] = {0};
float last_hit_height = h_threshold;
float hit_heights[num_hits_to_keep] = {0};
float hit_lengths[num_hits_to_keep] = {0};

float mean(float* l, int size_l){
    return((sum(l,size_l))/((float)size_l));
}
float standard_dev(float* l, int size_l){
    float var = 0;
    float m = mean(l, size_l);
    for (int i = 0; i < size_l; i ++){
        var += (l[i]-m)*(l[i]-m);
    } 
    return(sqrt((var/size_l)));
}

float sum(float* l, int size_l){
    float sum = 0;
    for (int i = 0; i < size_l; i ++){
        sum += l[i];
    }
    return(sum);
}


/* determines if a maximum we just saw was large enough to be a hit
 using a threshold for the height of the maximum and a threshold
 for the distance between the maximum and the most recent minimum;
 being bigger than either counts as a hit */
bool is_hit(int max_t, float cur_min, float max_val){
    bool is_hit = false;

    float length = max_val - cur_min;
    int time_since_hit = max_t - last_hit_time;

    int hit_i = hit_counter % num_hits_to_keep;

    float std = 0;
    float std_length = 0;
    int hit_len = min(num_hits_to_keep, hit_counter);
    if (hit_len > 3) {
        // calculate the standard deviation of the most recent hits
        // we will give it that much slack in the thresholds
        float hit_l[hit_len];
        float hit_h[hit_len];
        for (int i = 0; i < hit_len; i ++){
           hit_l[i] = hit_lengths[(hit_i -1 - i + num_hits_to_keep)%num_hits_to_keep];
           hit_h[i] = hit_heights[(hit_i -1 - i + num_hits_to_keep)%num_hits_to_keep];
        }
        std = standard_dev(hit_h, hit_len);
        std_length = standard_dev(hit_l, hit_len);
        //change threshold if started out too low
        if (threshold < mean(hit_l, hit_len) - 2 * std_length){
            threshold = mean(hit_l, hit_len) - 2 * std_length;
        }
    }
    // collect previous hit lengths and heights and calculate their weights
    // which are decaying exponentially with time
    const int max_ws_size = 20;
    float recent_max[max_ws_size] = {0};
    float recent_max_length[max_ws_size] = {0};
    float weights[max_ws_size] = {0};

    int prev_hit_time = max_t;
    int r_i = 0;
    for (int k = 0; k < hit_len; k++){
        int j = num_hits_to_keep - 1 - k; //go in reverse order
        int m = (hit_i -1 - k + num_hits_to_keep) % num_hits_to_keep;
        if (prev_hit_time  - hit_times[m] > 800){
            break;
        }
        if (max_t - hit_times[m] < 2000.0){
            float dj = (float)(max_t - hit_times[m]);
            float sigma = 2500.0;
            float e = 2.718281828459045;//the constant e
            float w = pow(e, -dj/sigma); //returns a double?

            recent_max_length[r_i] = w * max(hit_lengths[m] - 1.5 * std_length, threshold);
            recent_max[r_i] = w * max(hit_heights[m] - 1.5 * std, h_threshold);
            weights[r_i] = w;
            r_i += 1;
        }
        prev_hit_time = hit_times[m];
    }
    prev_hit_time = max_t;

    // pad if there are not enough; this should make the threshold drop
    // if there haven't been hits in a while
    // perhaps subtracting a moving average might also work but I didn't try it
    while (r_i < 5){
        recent_max_length[r_i] = threshold;
        recent_max[r_i] = h_threshold;
        weights[r_i] = 1;
        r_i += 1;
    }

    // the two thresholds
    float cur_threshold =sum(recent_max, max_ws_size) / sum(weights, max_ws_size);
    float cur_threshold_length =sum(recent_max_length, max_ws_size) / sum(weights, max_ws_size);
    if (hit_counter > 0){
        cur_threshold_length = min(cur_threshold_length, 0.45 * last_hit);
    }
    if ((max_val > cur_threshold) || (length > cur_threshold_length)){
            last_hit = length;
            hit_lengths[hit_i] = length;

            last_hit_time = max_t;
            hit_times[hit_i] = max_t;

            last_hit_height = max_val;
            hit_heights[hit_i] = last_hit_height;

            is_hit = true;
    }

    return(is_hit);
}

float mean_z = 0;
int last_t = 0;

/* processes the points one by one; calls updateCounter() upon hit */
float process(int t, int z_int){
    float z = (float) z_int;
    float filtered_z = 0;

    // calculate the mean for the first num_points_mean points
    if (num_points < num_points_mean){
        mean_z += z / ((float)num_points_mean);
        num_points += 1;  
        return(0);
    }
    else {
        if (last_t > t){
            t = last_t + 1; // in case of misread
        }
        z = z - mean_z;
        z = z > 0 ? z : -z;
        float filtered_z = get_filtered(z);
        if (num_points < num_points_limit){
            num_points += 1;
        }

        ws_index = cur_i % window_size;
        hws_index = cur_i % half_ws;

        vals[ws_index] = filtered_z;
        slopes[ws_index] = get_slope(vals);
        tvals[ws_index] = t;


        get_maxima(t); //return values in get_maxima_ret_vals
        if (get_maxima_ret_vals.is_max){
            if (is_hit(get_maxima_ret_vals.max_t, get_maxima_ret_vals.cur_min_copy,
                        get_maxima_ret_vals.max_val)){
                //It's a hit!!!
                updateCounter(t,z);
            }
        }
        cur_i += 1;
    }
    last_t = t;
    return(filtered_z);
}    
