#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

enum { MAX_WIDTH = 200, MAX_HEIGHT = 100, MAX_PIXELS = MAX_WIDTH * MAX_HEIGHT };

/* Target frame period — same as the old usleep(16000). */
enum { FRAME_NS = 16 * 1000 * 1000 };

static int width = 80;
static int height = 24;
static volatile sig_atomic_t sizeDirty = 1;

static float A, B, C;
static float zBuffer[MAX_PIXELS];
static char buffer[MAX_PIXELS];
static char frameOut[MAX_PIXELS + MAX_HEIGHT];

static const char BACKGROUND = '.';
static const float DISTANCE_FROM_CAM = 100.f;
static const float K1 = 40.f;

/* Rotation matrix rows after updateRotation(). */
static float R00, R01, R02;
static float R10, R11, R12;
static float R20, R21, R22;

static void restoreTerminal(void) {
  printf("\x1b[?25h\x1b[0m\x1b[?1049l");
  fflush(stdout);
}

static void onExitSignal(int sig) {
  (void)sig;
  restoreTerminal();
  _exit(0);
}

static void onWinch(int sig) {
  (void)sig;
  sizeDirty = 1;
}

static void syncTerminalSize(void) {
  struct winsize ws;
  int cols = 80;
  int rows = 24;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 &&
      ws.ws_row > 0) {
    cols = ws.ws_col;
    rows = ws.ws_row;
  }

  if (cols > MAX_WIDTH) {
    cols = MAX_WIDTH;
  }
  if (rows > MAX_HEIGHT) {
    rows = MAX_HEIGHT;
  }
  if (cols < 20) {
    cols = 20;
  }
  if (rows < 10) {
    rows = 10;
  }

  width = cols;
  height = rows;
  sizeDirty = 0;
}

static void updateRotation(void) {
  float sA = sinf(A), cA = cosf(A);
  float sB = sinf(B), cB = cosf(B);
  float sC = sinf(C), cC = cosf(C);

  R00 = cB * cC;
  R01 = sA * sB * cC + cA * sC;
  R02 = -cA * sB * cC + sA * sC;
  R10 = -cB * sC;
  R11 = cA * cC - sA * sB * sC;
  R12 = sA * cC + cA * sB * sC;
  R20 = sB;
  R21 = -sA * cB;
  R22 = cA * cB;
}

static void renderFace(int half, float horizontalOffset, char ch, int i0,
                       int j0, int k0, int diu, int dju, int dku, int div,
                       int djv, int dkv) {
  float Xu = R00 * diu + R01 * dju + R02 * dku;
  float Yu = R10 * diu + R11 * dju + R12 * dku;
  float Zu = R20 * diu + R21 * dju + R22 * dku;
  float Xv = R00 * div + R01 * djv + R02 * dkv;
  float Yv = R10 * div + R11 * djv + R12 * dkv;
  float Zv = R20 * div + R21 * djv + R22 * dkv;

  float X = R00 * i0 + R01 * j0 + R02 * k0;
  float Y = R10 * i0 + R11 * j0 + R12 * k0;
  float Z = R20 * i0 + R21 * j0 + R22 * k0;

  const float cx = (float)(width / 2) + horizontalOffset;
  const float cy = (float)(height / 2);
  const float scaleX = K1 * 2.f;
  const float scaleY = K1;
  const int w = width;
  const int h = height;

  int n = half * 2;
  for (int u = 0; u < n; u++) {
    float x = X, y = Y, z = Z;
    for (int v = 0; v < n; v++) {
      float zCam = z + DISTANCE_FROM_CAM;
      if (zCam > 0.f) {
        float ooz = 1.f / zCam;
        int xp = (int)(cx + scaleX * ooz * x);
        int yp = (int)(cy + scaleY * ooz * y);
        if ((unsigned)xp < (unsigned)w && (unsigned)yp < (unsigned)h) {
          int idx = xp + yp * w;
          if (ooz > zBuffer[idx]) {
            zBuffer[idx] = ooz;
            buffer[idx] = ch;
          }
        }
      }
      x += Xv;
      y += Yv;
      z += Zv;
    }
    X += Xu;
    Y += Yu;
    Z += Zu;
  }
}

static void renderCube(int half, float horizontalOffset) {
  /* Face parameterization matches the original six surfaces. */
  renderFace(half, horizontalOffset, '@', -half, -half, -half, 1, 0, 0, 0, 1,
             0);
  renderFace(half, horizontalOffset, '$', half, -half, -half, 0, 0, 1, 0, 1, 0);
  renderFace(half, horizontalOffset, '~', -half, -half, half, 0, 0, -1, 0, 1,
             0);
  renderFace(half, horizontalOffset, '#', half, -half, half, -1, 0, 0, 0, 1, 0);
  renderFace(half, horizontalOffset, ';', -half, -half, half, 1, 0, 0, 0, 0,
             -1);
  renderFace(half, horizontalOffset, '+', -half, half, -half, 1, 0, 0, 0, 0, 1);
}

static void presentFrame(void) {
  /* Home cursor, then overwrite the same rows. No '\n' after the last row —
     that would scroll the terminal when height == window rows. */
  int n = 0;
  frameOut[n++] = '\x1b';
  frameOut[n++] = '[';
  frameOut[n++] = 'H';

  for (int row = 0; row < height; row++) {
    memcpy(frameOut + n, buffer + row * width, (size_t)width);
    n += width;
    if (row + 1 < height) {
      frameOut[n++] = '\n';
    }
  }

  fwrite(frameOut, 1, (size_t)n, stdout);
  fflush(stdout);
}

static long elapsedNs(const struct timespec *start,
                      const struct timespec *end) {
  return (end->tv_sec - start->tv_sec) * 1000000000L +
         (end->tv_nsec - start->tv_nsec);
}

static void sleepRemaining(const struct timespec *frameStart) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  long used = elapsedNs(frameStart, &now);
  long left = (long)FRAME_NS - used;
  if (left <= 0) {
    return;
  }

  struct timespec req;
  req.tv_sec = left / 1000000000L;
  req.tv_nsec = left % 1000000000L;
  nanosleep(&req, NULL);
}

int main(void) {
  setvbuf(stdout, NULL, _IOFBF, 1 << 16);
  signal(SIGINT, onExitSignal);
  signal(SIGTERM, onExitSignal);
  signal(SIGWINCH, onWinch);
  atexit(restoreTerminal);

  syncTerminalSize();

  /* Alternate screen: animation stays in one buffer, scrollback untouched. */
  printf("\x1b[?1049h\x1b[2J\x1b[H\x1b[?25l");
  fflush(stdout);

  while (1) {
    struct timespec frameStart;
    clock_gettime(CLOCK_MONOTONIC, &frameStart);

    if (sizeDirty) {
      syncTerminalSize();
    }

    size_t pixels = (size_t)width * (size_t)height;
    memset(buffer, BACKGROUND, pixels);
    memset(zBuffer, 0, pixels * sizeof zBuffer[0]);
    updateRotation();

    renderCube(20, -2.f * 20.f);
    renderCube(10, 1.f * 10.f);
    renderCube(5, 8.f * 5.f);

    presentFrame();

    A += 0.05f;
    B += 0.05f;
    C += 0.01f;

    sleepRemaining(&frameStart);
  }

  return 0;
}
