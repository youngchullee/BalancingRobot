/* 
 * The code is released under the GNU General Public License.
 * Developed by Kristian Lauszus
 * This is the algorithm for my balancing robot/segway.
 * It is controlled by a PS3 Controller via bluetooth.
 * The remote control code can be found at my other github repository: https://github.com/TKJElectronics/BalancingRobot
 * For details, see http://blog.tkjelectronics.dk
 */

#include "BalancingRobot.h"
#include "Encoder.h"

/* Serial communication */
Serial xbee(p13,p14); // For setting PID values
Serial debug(USBTX, USBRX); // USB

/* Setup encoders */
Encoder leftEncoder(p29,p30);
Encoder rightEncoder(p27,p28);

/* Timer instance */
Timer t;

int main() {
    xbee.baud(115200);
    debug.baud(115200);
    
    leftPWM.period(0.00005); //The motor driver can handle pwm values up to 20kHz (1/20000=0.00005)
    rightPWM.period(0.00005);
    
    calibrateSensors(); // Calibrate the gyro and accelerometer relative to ground
    
    xbee.printf("Initialized\n");
    processing(); // Send output to processing application
    
    // Start timing
    t.start();
    loopStartTime = t.read_us();
    timer = loopStartTime;
    
    /*
     move(both,forward,0.5);
     while(1)
     debug.printf("%i,%i\n",leftEncoder.read(),rightEncoder.read());
     */
    
    while (1) {
        // See my guide for more info about calculation the angles and the Kalman filter: http://arduino.cc/forum/index.php/topic,58048.0.html
        accYangle = getAccY();
        gyroYrate = getGyroYrate();
        
        pitch = kalman(accYangle, gyroYrate, (t.read_us() - timer)); // calculate the angle using a Kalman filter
        timer = t.read_us();
        
        if (xbee.readable())
            receiveXbee();
        
        //debug.printf("AccYangle: %f, Pitch: %f\n",accYangle,pitch);
        
        if (pitch < 75 || pitch > 105) //Stop if falling or laying down
            stopAndReset();
        else
            PID(targetAngle,targetPosition,leftEncoder.read(),rightEncoder.read());
        
        lastLoopUsefulTime = t.read_us() - loopStartTime;
        if (lastLoopUsefulTime < STD_LOOP_TIME)
            wait_us(STD_LOOP_TIME - lastLoopUsefulTime);
        lastLoopTime = t.read_us() - loopStartTime;//only used for debugging
        loopStartTime = t.read_us();
        
        //debug.printf("%i,%i\n",lastLoopUsefulTime,lastLoopTime);
    }
}
void stopAndReset() {
    stop(both);
    lastError = 0;
    iTerm = 0;
    /*
     leftLastError = 0;
     iTermLeft = 0;
     rightLastError = 0;
     iTermRight = 0;
     */
    leftEncoder.reset(0);
    rightEncoder.reset(0);
}
void receiveXbee() {
    char input[16]; // The serial buffer is only 16 characters, so the data has to be split up
    int i = 0;
    while (xbee.readable()) {
        input[i] = xbee.getc();
        i++;
    }
    //debug.printf("Input: %s\n",input);
    if (input[0] == '1') {
        strtok(input, ",");//Ignore '1'
        Kp = atof(strtok(NULL, ","));
        Ki = atof(strtok(NULL, ","));
        Kd = atof(strtok(NULL, ","));
    } else if (input[0] == '2') {
        strtok(input, ",");//Ignore '2'        
        targetAngle = atof(strtok(NULL, ","));
        if (targetAngle < 75 || targetAngle > 105)
            targetAngle = 90;
    } else if (input[0] == '3') {
        strtok(input, ",");//Ignore '3'
        wheelKp = atof(strtok(NULL, ","));
        wheelKd = atof(strtok(NULL, ","));
    } else if (input[0] == '4') {
        strtok(input, ",");//Ignore '4'
        targetPosition = atoi(strtok(NULL, ","));
        processing(); // Send output to processing application
    } else if (input[0] == 'A') {
        stopAndReset();
        while (xbee.getc() != 'C'); // Wait until continue is pressed again
    } else if (input[0] == 'G') { // The processing application sends this at startup
        processing(); // Send output to processing application
        xbee.printf("Initialized\n");
    }
}

void processing() {
    /* Send output to processing application */
    xbee.printf("%6.3f,%6.3f,%6.3f,%6.3f,",Kp,Ki,Kd,targetAngle);
    xbee.printf("%6.3f,%6.3f,%i\n",wheelKp,wheelKd,targetPosition);
}

void PID(double restAngle, long targetPosition, long leftPosition, long rightPosition) {
    /* PID loop based on angle */       
    double error = (restAngle - pitch)/100;
    double pTerm = Kp * error;
    iTerm += Ki * error;
    double dTerm = Kd * (error - lastError);
    lastError = error;
    
    double PIDValue = pTerm + iTerm + dTerm;
    
    //debug.printf("PIDValue: %f Time: %i,%i\n",PIDValue,lastLoopUsefulTime,lastLoopTime);
    //debug.printf("Pitch: %5.2f\tPIDValue:  %5.2f\tpTerm: %5.2f\tiTerm: %5.2f\tdTerm: %5.2f\tKp: %5.2f\tKi: %5.2f\tKd: %5.2f\n",pitch,PIDValue,pTerm,iTerm,dTerm,Kp,Ki,Kd);
    
    /* PID loop based on encoders */
    /*
     double wheelSpeed = (targetPosition-(leftPosition + rightPosition)/2)/100;
     double pTermLeft = wheelKp * wheelSpeed;
     double dTermLeft = wheelKd * (wheelSpeed - lastWheelSpeed);
     lastWheelSpeed = wheelSpeed;
     
     PIDValue += pTermLeft + dTermLeft;
     */
    //debug.printf("PIDValue: %f,PIDValueLeft: %f, PIDValueRight: %f\n",PIDValue,PIDValueLeft,PIDValueRight);    
    
    double PIDLeft = PIDValue;
    double PIDRight = PIDValue;
    
    PIDLeft *= 0.9;
    
    //Set PWM Values
    if (PIDLeft >= 0)
        move(left, forward, PIDLeft);        
    else
        move(left, backward, PIDLeft * -1);        
    if  (PIDRight >= 0)
        move(right, forward, PIDRight);
    else
        move(right, backward, PIDRight * -1);
}
double kalman(double newAngle, double newRate, double dtime) {
    // KasBot V2  -  Kalman filter module - http://www.arduino.cc/cgi-bin/yabb2/YaBB.pl?num=1284738418 - http://www.x-firm.com/?page_id=145
    // with slightly modifications by Kristian Lauszus
    dt = dtime / 1000000; // Convert from microseconds to seconds
    
    //Discrete Kalman filter time update equations - Time Update ("Predict")
    //Update xhat - Project the state ahead
    angle += dt * (newRate - bias);
    
    //Update estimation error covariance - Project the error covariance ahead
    P_00 += -dt * (P_10 + P_01) + Q_angle * dt;
    P_01 += -dt * P_11;
    P_10 += -dt * P_11;
    P_11 += +Q_gyro * dt;
    
    //Discrete Kalman filter measurement update equations - Measurement Update ("Correct")
    //Calculate Kalman gain - Compute the Kalman gain
    S = P_00 + R_angle;
    K_0 = P_00 / S;
    K_1 = P_10 / S;
    
    //Calculate angle and resting rate - Update estimate with measurement zk
    y = newAngle - angle;
    angle += K_0 * y;
    bias += K_1 * y;
    
    //Calculate estimation error covariance - Update the error covariance
    P_00 -= K_0 * P_00;
    P_01 -= K_0 * P_01;
    P_10 -= K_1 * P_00;
    P_11 -= K_1 * P_01;
    
    return angle;
}
double getGyroYrate() {
    //(gyroXadc-gyroZeroY)/Sensitivity (In quids) - Sensitivity = 0.00333/3.3*4095=4.132227273
    double gyroRate = -(((gyroY.read()*4095) - zeroValues[0]) / 4.132227273);
    return gyroRate;
}
double getAccY() {
    //(accXadc-accZeroX)/Sensitivity (In quids) - Sensitivity = 0.33/3.3*4095=409.5
    double accXval = ((accX.read()*4095) - zeroValues[1]) / 409.5;
    double accYval = ((accY.read()*4095) - zeroValues[2]) / 409.5;
    accYval--;//-1g when lying down
    double accZval = ((accZ.read()*4095) - zeroValues[3]) / 409.5;
    
    double R = sqrt(pow(accXval, 2) + pow(accYval, 2) + pow(accZval, 2)); // Calculate the force vector
    double angleY = acos(accYval / R) * RAD_TO_DEG;
    
    return angleY;
}
void calibrateSensors() {
    onboardLED1 = 1;
    onboardLED2 = 1;
    onboardLED3 = 1;
    onboardLED4 = 1;
    
    double adc[4];
    for (uint8_t i = 0; i < 100; i++) { // Take the average of 100 readings
        adc[0] += gyroY.read()*4095;
        adc[1] += accX.read()*4095;
        adc[2] += accY.read()*4095;
        adc[3] += accZ.read()*4095;
        wait_ms(10);
    }
    zeroValues[0] = (adc[0] / 100); // Gyro X-axis
    zeroValues[1] = (adc[1] / 100); // Accelerometer X-axis
    zeroValues[2] = (adc[2] / 100); // Accelerometer Y-axis
    zeroValues[3] = (adc[3] / 100); // Accelerometer Z-axis
    
    onboardLED1 = 0;
    onboardLED2 = 0;
    onboardLED3 = 0;
    onboardLED4 = 0;
}
void move(Motor motor, Direction direction, float speed) { // speed is a value in percentage (0.0f-1.0f)
    if (motor == right) {
        leftPWM = speed;
        if (direction == forward) {
            leftA = 0;
            leftB = 1;
        } else if (direction == backward) {
            leftA = 1;
            leftB = 0;
        }
    } else if (motor == left) {
        rightPWM = speed;
        if (direction == forward) {
            rightA = 0;
            rightB = 1;
        } else if (direction == backward) {
            rightA = 1;
            rightB = 0;
        }
    } else if (motor == both) {
        leftPWM = speed;
        rightPWM = speed;
        if (direction == forward) {
            leftA = 0;
            leftB = 1;
            
            rightA = 0;
            rightB = 1;
        } else if (direction == backward) {
            leftA = 1;
            leftB = 0;
            
            rightA = 1;
            rightB = 0;
        }
    }
}
void stop(Motor motor) {
    if (motor == left) {
        leftPWM = 1;
        leftA = 1;
        leftB = 1;
    } else if (motor == right) {
        rightPWM = 1;
        rightA = 1;
        rightB = 1;
    } else if (motor == both) {
        leftPWM = 1;
        leftA = 1;
        leftB = 1;
        
        rightPWM = 1;
        rightA = 1;
        rightB = 1;
    }
}