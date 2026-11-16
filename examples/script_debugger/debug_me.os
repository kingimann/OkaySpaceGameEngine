// A small script to practise the debugger on. Press Play, then click a line number to set a breakpoint.
var frames = 0;
var total = 0;

function Square(n) {
  var result = n * n;
  return result;
}

function AddScore(points) {
  var bonus = Square(points);
  total = total + bonus;
  return total;
}

function update(dt) {
  frames = frames + 1;
  if (frames % 60 == 0) {
    var gained = AddScore(frames / 60);
    print("score is now " + total);
  }
}
