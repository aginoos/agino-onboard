.pragma library
function key(label,sym,units,name){return {label:label,sym:sym,units:units||1,name:name||label}}
function letters(s){return s.split("").map(c=>key(c.toUpperCase(),c.charCodeAt(0)))}
function rows(fn){
 let result=[];
 if(fn){let f=[key("Esc",0xff1b)];for(let i=1;i<=12;i++)f.push(key("F"+i,0xffbd+i,1));f.push(key("Del",0xffff,2));result.push(f)}
 let n=(fn?[]:[key("Esc",0xff1b)]).concat([key("`",96)],"1234567890-=".split("").map(c=>key(c,c.charCodeAt(0))));n.push(key("⌫",0xff08,fn?2:1,"Backspace"));result.push(n);
 let q=[key("Tab",0xff09)].concat(letters("qwertyuiop"),[key("[",91),key("]",93),key("\\",92)]);if(!fn)q.push(key("Del",0xffff));else q[q.length-1].units=2;result.push(q);
 result.push([key("🌐",0,1.75,"Language")].concat(letters("asdfghjkl"),[key(";",59),key("'",39),key("Enter",0xff0d,2.25)]));
 result.push([key("Shift",0xffe1,2.5,"Shift-L")].concat(letters("zxcvbnm"),[key(",",44),key(".",46),key("/",47),key("Shift",0xffe2,2.5,"Shift-R")]));
 result.push([key("Ctrl",0xffe3,1.5,"Ctrl-L"),key("Super",0xffeb),key("Alt",0xffe9,1,"Alt-L"),key("空格",32,7.5,"Space"),key("Alt",0xffea,1,"Alt-R"),key("Ctrl",0xffe4,2,"Ctrl-R"),key("⌄",0,1,"Hide")]);
 return result;
}
function start(row,index){let u=0;for(let n=0;n<index;n++)u+=row[n].units;return u}
function alternatives(sym){let map={97:["á","à","â","ä"],101:["é","è","ê","ë"],105:["í","ì","î","ï"],111:["ó","ò","ô","ö"],117:["ú","ù","û","ü"],110:["ñ"],99:["ç"],46:["…"],45:["—","–"]};return map[sym]||[]}
