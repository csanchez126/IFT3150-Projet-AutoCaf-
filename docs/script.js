const anchors = document.querySelectorAll(".nav a");
const li = document.querySelectorAll("li");
const sections = document.querySelectorAll("section");

tabAnimation(document.querySelector(".li-presentation"))

console.log("CLICKED");
function navigation(e){
  //Tab styling
  let name = this.href.split("#")[1];
  let li = document.querySelector(".li-"+name);
  tabAnimation(li)
  console.log(li);
}
function tabAnimation(clicked){
  for(var i=0;i<li.length; i+=1){
    li[i].style.width = "100%";
  }
  clicked.style.width = "115%";

}


(function(){for(var i=0; i<anchors.length; i+=1){
  console.log("ancho");
  anchors[i].addEventListener("click", navigation);
}})();
//anchors.forEach(a => a.addEventListener("click", navigation));

// Tiré de http://stackoverflow.com/questions/17534661/make-anchor-link-go-some-pixels-above-where-its-linked-to
// pour le offset du haut lors d'un changement de section
function offsetAnchor() {
    if(location.hash.length !== 0) {
        window.scrollTo(window.scrollX, window.scrollY - 50);
    }
}
window.addEventListener("hashchange", offsetAnchor);
