var globalSet;
var globalGet;

function main() {
  var a = "initial";

  function set() { a = "updated"; }
  function get() { console.log(a); }

  globalSet = set;
  globalGet = get;
}

main();
globalSet();
globalGet();