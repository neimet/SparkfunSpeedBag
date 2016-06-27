from scipy.interpolate import UnivariateSpline
from scipy.signal import wiener, filtfilt, butter, gaussian, freqz
from scipy.ndimage import filters
import scipy.optimize as op
import matplotlib.pyplot as plt
import sys
import numpy as np
import matplotlib.pyplot as plt



# We will use a Butterworth filter of order 9, normalized cutoff frequency 4.0/nyf
# where nyf is the Nyquist frequency (250)
# since our samples aren't exactly 500hz, not exactly accurate

# constants for the filter, which we can also get by
# b,a = butter(9,4/250.0)
b = [3.46829595e-15,3.12146636e-14,1.24858654e-13,
    2.91336860e-13,4.37005290e-13,4.37005290e-13,
    2.91336860e-13,1.24858654e-13,3.12146636e-14,
    3.46829595e-15]
a =  [-8.71053517,33.72604936,-76.18343955,
    110.64383341,-107.14182626,69.17600449,
    -28.71584846,6.95439462,-0.74863243]
a = a[1:]
a = [-8.7105351748815316, 33.726049364325746, -76.183439550219617, 110.64383340913214, -107.14182626366458, 69.176004487367535, -28.715848459821281, 6.9543946168387691, -0.74863242907538308] 
order = 9
raw_v = [0] * (order + 1)
filtered_v = [0] * order
raw_i = 0
filtered_i = 0


# current number of points; will top out at num_points_limit
num_points = 0
num_points_limit = 10000

# number of points to use to calibrate;
# we will calculate the mean of these many points and subtract them
# from subsequent values
# 100 corresponds to about 200 milliseconds, which seems okay
num_points_mean = 100
mean_z = 0
last_t = 0

def mean(l):
    return sum(l)/float(len(l))

def standard_dev(l):
    var = 0.0
    m = mean(l)
    for x in l:
        var += (x-m)**2
    return np.sqrt((var/ len(l)))


# This will calculate the Butterworth filter of order 9 
# and cutoff frequency 0.016
def get_filtered(v):
    global raw_v, filtered_v, raw_i, filtered_i
    raw_v[raw_i] = v
    filtered_val = 0
    for j in range(order):
        filtered_val -= a[j] * filtered_v[(filtered_i  - 1- j) % order]
        filtered_val += b[j] * raw_v[(raw_i-1 - j)%(order+1)]
    filtered_val += b[order] * raw_v[raw_i]
    filtered_v[filtered_i] = filtered_val
    raw_i = (raw_i + 1 ) % (order + 1)
    filtered_i = (filtered_i + 1 ) % (order)
    return filtered_val


#holds the number of hits
hit_counter = 0

def updateCounter(t,z):
    global hit_counter
    hit_counter += 1
    print t,z, hit_counter


window_size = 44
half_ws = window_size / 2
#window size for averaging to get current minimum value
ws_ave = 10
std = window_size/3.0
kl = gaussian(window_size,std)

# get the slope at a particular data point by a subtracting a weighted
# average of the first half of a window of points from a weighted average
# of the second half of a window of points
def get_slope(vals):
    slope = 0
    for i in range(half_ws):
        slope += kl[(window_size - 1 - i)%window_size] * vals[(ws_index-1-i) % window_size] #second half
        slope -= kl[i] * vals[(ws_index+i)% window_size] # first half
    return slope

# sliding window of values (to get maxima)
vals = [0] * window_size
tvals = [0] * window_size
slopes = [0] * window_size
maxima = [False] * half_ws
default_min = 10000
cur_min = default_min

#for graphing
max_locs = []
max_ts = []


# finds local maxima by looking at when the window of slopes go from being
# mostly positive to mostly negative; also finds the most recent local minimum
# tha corresponds to the local maximum
def get_maxima(cur_t):
    global num_points, maxima, cur_min, max_val
    global max_locs
    is_max = False
    max_val = - default_min
    max_t = 0
    cur_min_copy = 0
    if (num_points > num_points_mean + window_size + 1):
        cm = 0
        for i in range(ws_ave):
            cm += vals[(ws_index + 1 + i)%window_size]/float(ws_ave)
        if cm < cur_min:
            cur_min = cm

        first_count = 0
        second_count = 0

        for i in range(half_ws):

            k1 = (ws_index + i)%window_size
            k2 = (ws_index - 1 -i)%window_size
            if slopes[k1] > 0.25:
                first_count += 1
            if slopes[k2] < -0.25:
                second_count += 1

        if first_count > half_ws -3 and second_count > half_ws -3:
            any_m = False
            for m in maxima:
                if m:
                    any_m = True
            if not any_m:
                is_max = True
                max_val = vals[(ws_index + 1)%window_size] 
                max_t = tvals[(ws_index + 1)%window_size]
                cur_min_copy = cur_min
                cur_min = default_min

                #for graphing
                max_locs.append(max_val)
                max_ts.append(tvals[(ws_index + 1)%window_size]) 

        maxima[hws_index] = is_max
    
    return is_max, max_val, cur_min_copy, max_t
        

default_threshold = 24
h_threshold = 35
num_hits_to_keep = 200
threshold = default_threshold 
last_hit = default_threshold
last_hit_time = 0
hit_times = [0] * num_hits_to_keep
last_hit_height = h_threshold
hit_heights = [0] * num_hits_to_keep
hit_lengths = [0] * num_hits_to_keep

# for graphing
thresholds = [] 
length_thresholds = [] 
hit_ts = []
hit_locs = []


# determines whether a local maximum is high enough to be a hit
# by calculating two thresholds: one for the absolute height
# and another for the length between the height and the local minimum
# right before it
def is_hit(max_t, cur_min, max_val):
    global last_hit_time, num_hits, threshold, last_hit, hit_times
    global hit_heights, hit_lengths
    global thresholds

    is_hit = False # return value
    
    length = max_val - cur_min
    time_since_hit = max_t - last_hit_time

    hit_i = hit_counter % num_hits_to_keep

    std = 0
    std_length = 0
    hit_len = min(num_hits_to_keep, hit_counter)
    if hit_len > 3:
        hit_l = [0] * hit_len
        hit_h = [0] * hit_len
        for i in range(hit_len):
           hit_l[i] = hit_lengths[(hit_i -1 - i)%num_hits_to_keep]
           hit_h[i] = hit_heights[(hit_i -1 - i)%num_hits_to_keep]
        std = standard_dev(hit_h)
        std_length = standard_dev(hit_l)
        # change threshold if started out too low
        if threshold < mean(hit_l) - 2 * std_length:
            threshold = mean(hit_l) - 2 * std_length

    # collect previous hit lengths and heights and calculate their weights
    recent_max = [0] * 20
    recent_max_length = [0] * 20
    weights = [0] * 20

    prev_hit_time = max_t
    r_i = 0
    for k in range(hit_len):
        j = num_hits_to_keep - 1 - k #go in reverse order
        m = (hit_i -1 - k) % num_hits_to_keep
        if prev_hit_time  - hit_times[m] > 800:
            break;
        if max_t - hit_times[m] < 2000.0:
            dj = max_t - hit_times[m]
            sigma = 2500.0
            w =np.exp(-dj  /  sigma)

            recent_max_length[r_i] = w * max(hit_lengths[m] - 1.5 * std_length, threshold)
            recent_max[r_i] = w * max(hit_heights[m] - 1.5 * std, h_threshold)
            weights[r_i] = w
            r_i += 1
        prev_hit_time = hit_times[m]
    prev_hit_time = max_t

    # pad if there are not enough; this should make the threshold drop
    # if there haven't been hits in a while
    # perhaps subtracting a moving average might also work but I didn't try it
    while r_i < 5:
        recent_max_length[r_i] = threshold
        recent_max[r_i] = h_threshold
        weights[r_i] = 1
        r_i += 1

    cur_threshold =sum(recent_max) / sum(weights) 
    cur_threshold_length =sum(recent_max_length) / sum(weights) 
    if hit_counter > 0:
        cur_threshold_length = min(cur_threshold_length, 0.45 * last_hit)
    if max_val > cur_threshold or length > cur_threshold_length:
            last_hit = length
            hit_lengths[hit_i] = length

            last_hit_time = max_t
            hit_times[hit_i] = max_t

            last_hit_height = max_val
            hit_heights[hit_i] = last_hit_height
            is_hit = True

            hit_ts.append(max_t)
            hit_locs.append(max_val)
    global thresholds
    thresholds.append((max_t,cur_threshold))
    length_thresholds.append(cur_threshold_length + cur_min)

    return is_hit

cur_i = 0
ws_index = 0
hws_index = 0

# runs everything for each data point
def process(t,z):
    global num_points, mean_z, vals, slopes, tvals, last_t, cur_i, ws_index, hws_index
    if num_points < num_points_mean:
        mean_z += z / float(num_points_mean)
        num_points += 1  
        return False
    else:
        if last_t > t:
            t = last_t + 1 # in case of misread
        z = abs(z - mean_z)
        filtered_z = get_filtered(z)
        if num_points < num_points_limit:
            num_points += 1  

        ws_index = cur_i % window_size
        hws_index = cur_i % half_ws

        vals[ws_index] = filtered_z
        slopes[ws_index] = get_slope(vals)
        tvals[ws_index] = t


        is_max, max_val, cur_min, max_t = get_maxima(t)
        if is_max:
            if is_hit(max_t, cur_min, max_val):
                updateCounter(t,z)
        cur_i += 1 
    last_t = t
    return filtered_z


# reads data and runs process for each data point
# also plots things
def test_all():
    if (len(sys.argv) != 2):
        print "Using default: 3-77hits.TXT"
        file_name = "3-77hits.TXT"
    else:
        file_name = str(sys.argv[1])
        print "Using file name: " + file_name


    accel_data = open("data/" + file_name, 'r')
    accel_data = accel_data.readlines()[3:]
    accel_data = accel_data[500:]
    prev_t = 0
    filtered = []
    ts = []


    prev_t = 0
    for line in accel_data:
        if ":" not in line and "initialized" not in line and "," in line:
            # otherwise, accel response timeout or initialized
            # in which case, do nothing for now
            vals = line.split(",")
            if (len(vals) == 4):
                vals = map(int,vals)
                cur_t = vals[0]
                cur_z = vals[3]
                filtered_z = process(cur_t, cur_z)
                filtered.append(filtered_z)
                if cur_t < prev_t:
                    cur_t = prev_t + 1
                ts.append(cur_t)
                prev_t = cur_t


    plt.plot(ts, filtered)
    plt.plot(list(x[0] for x in thresholds), list(x[1] for x in thresholds), color = "magenta")
    plt.plot(list(x[0] for x in thresholds), list(x for x in length_thresholds), color = "green")
    plt.scatter(max_ts, max_locs, color = "red")
    plt.scatter(hit_ts, hit_locs, color = "cyan")

    plt.show()
test_all()
