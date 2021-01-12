function outer() {
  var x = "before";
  function inner() {
    x = "assigned";
  }
  inner();
  console.log(x);
}
outer();